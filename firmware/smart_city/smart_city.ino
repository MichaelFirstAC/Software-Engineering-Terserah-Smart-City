/*
  Smart City prototype - Arduino Uno R3
  Team B5BC8 "Terserah" - COMP6100001 Software Engineering

  Modules (each: Sensor Interface -> Decision Logic -> Actuator Control):
    1. Smart streetlight  : LDR (A0) + PIR (D2)  -> LED (D8)
    2. Smart parking      : HC-SR04 #1           -> green/red LED
    3. Smart traffic light: HC-SR04 #2 + #3      -> 2 x (R,Y,G) LEDs
  Shared Serial Communication Module -> Python dashboard (9600 baud).

  Fail-safe defaults: light OFF, parking FREE, traffic standard timing.
*/

// ---------------- Pins ----------------
const uint8_t PIN_LDR     = A0;
const uint8_t PIN_PIR     = 2;
const uint8_t PIN_STL_LED = 8;

const uint8_t PK_TRIG = 3, PK_ECHO = 4, PK_GREEN = 5, PK_RED = 6;

const uint8_t TA_TRIG = 9,  TA_ECHO = 10;   // direction A sensor
const uint8_t TB_TRIG = 11, TB_ECHO = 12;   // direction B sensor
const uint8_t A_R = A1, A_Y = A2, A_G = A3; // direction A lights
const uint8_t B_R = A4, B_Y = A5, B_G = 7;  // direction B lights

// ---------------- Tunable constants ----------------
// Streetlight
int DARK_ON  = 300;                     // dark when LDR < DARK_ON  (change with "T=")
int DARK_OFF = 400;                     // light again when LDR > DARK_OFF (hysteresis)
const unsigned long PIR_WARMUP_MS = 30000UL;  // ignore PIR after boot
const unsigned long HOLD_MS       = 10000UL;  // keep light on after last motion
const unsigned long PIR_STUCK_MS  = 60000UL;  // PIR HIGH this long = fault -> light OFF

// Parking
const int  PK_OCC_CM  = 15;             // occupied if distance < 15 cm
const int  PK_FREE_CM = 20;             // free if distance >= 20 cm (or invalid)
const int  PK_CONFIRM = 3;              // consecutive readings needed to change state

// Traffic
const int  DETECT_CM = 40;              // vehicle present if 2..40 cm
const int  WINDOW    = 10;              // readings kept per direction
const int  LEAD_MIN  = 3;               // score lead needed for extension
const unsigned long BASE_GREEN_MS = 10000UL;
const unsigned long EXT_MS        = 5000UL;   // base + ext = 15 s cap
const unsigned long YELLOW_MS     = 2000UL;

// Serial
const unsigned long HEARTBEAT_MS = 1000UL;    // resend status (resync after reconnect)

// ---------------- Streetlight state ----------------
bool isDark = false;
bool stlOn = false;
bool pirFault = false;
unsigned long lastMotion = 0;
unsigned long pirHighSince = 0;
bool pirWasHigh = false;
int ldrValue = 0;

// ---------------- Parking state ----------------
bool parkOccupied = false;
int  parkCount = 0;          // consecutive readings disagreeing with current state
int  parkDist = -1;

// ---------------- Traffic state ----------------
enum Phase { A_GREEN, A_YELLOW, B_GREEN, B_YELLOW };
Phase phase = A_GREEN;
unsigned long phaseStart = 0;
unsigned long greenLen = BASE_GREEN_MS;
bool extended = false;
bool winA[WINDOW], winB[WINDOW];
uint8_t winIdx = 0;
int distA = -1, distB = -1;

// ---------------- Serial reporting ----------------
String lastSL, lastPK, lastTR;
unsigned long lastHeartbeat = 0;

// =====================================================
// Sensor interface helpers
// =====================================================
int readCm(uint8_t trig, uint8_t echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  unsigned long d = pulseIn(echo, HIGH, 12000UL);  // ~2 m max
  if (d == 0) return -1;                           // invalid / no echo
  return (int)(d / 58UL);
}

int countTrue(const bool *w) {
  int c = 0;
  for (int i = 0; i < WINDOW; i++) if (w[i]) c++;
  return c;
}

// =====================================================
// Module 1: Smart streetlight
// =====================================================
void updateStreetlight(unsigned long now) {
  ldrValue = analogRead(PIN_LDR);

  // darkness with hysteresis
  if (!isDark && ldrValue < DARK_ON)  isDark = true;
  if (isDark  && ldrValue > DARK_OFF) isDark = false;

  // PIR (ignored during warm-up)
  bool pir = digitalRead(PIN_PIR) == HIGH;
  if (now < PIR_WARMUP_MS) pir = false;

  if (pir) {
    if (!pirWasHigh) pirHighSince = now;
    pirWasHigh = true;
    if (now - pirHighSince > PIR_STUCK_MS) pirFault = true;   // stuck HIGH
    lastMotion = now;
  } else {
    pirWasHigh = false;
    pirFault = false;
  }

  bool recentMotion = (lastMotion != 0) && (now - lastMotion < HOLD_MS);
  stlOn = isDark && recentMotion && !pirFault;   // dark AND motion (with hold)
  digitalWrite(PIN_STL_LED, stlOn ? HIGH : LOW);
}

// =====================================================
// Module 2: Smart parking
// =====================================================
void updateParking() {
  parkDist = readCm(PK_TRIG, PK_ECHO);

  bool wantOcc;
  if (parkDist < 0) {
    wantOcc = false;                       // invalid -> fail-safe FREE
  } else if (parkDist < PK_OCC_CM) {
    wantOcc = true;
  } else if (parkDist >= PK_FREE_CM) {
    wantOcc = false;
  } else {
    wantOcc = parkOccupied;                // dead band: keep current state
  }

  if (wantOcc != parkOccupied) {
    parkCount++;
    if (parkCount >= PK_CONFIRM) {
      parkOccupied = wantOcc;
      parkCount = 0;
    }
  } else {
    parkCount = 0;
  }

  digitalWrite(PK_GREEN, parkOccupied ? LOW : HIGH);
  digitalWrite(PK_RED,   parkOccupied ? HIGH : LOW);
}

// =====================================================
// Module 3: Smart traffic light
// =====================================================
void setTraffic(Phase p) {
  digitalWrite(A_R, (p == B_GREEN || p == B_YELLOW) ? HIGH : LOW);
  digitalWrite(A_Y, p == A_YELLOW ? HIGH : LOW);
  digitalWrite(A_G, p == A_GREEN  ? HIGH : LOW);
  digitalWrite(B_R, (p == A_GREEN || p == A_YELLOW) ? HIGH : LOW);
  digitalWrite(B_Y, p == B_YELLOW ? HIGH : LOW);
  digitalWrite(B_G, p == B_GREEN  ? HIGH : LOW);
}

void sampleTraffic(bool isA) {
  if (isA) {
    distA = readCm(TA_TRIG, TA_ECHO);
    winA[winIdx] = (distA >= 2 && distA <= DETECT_CM);   // invalid = no vehicle
  } else {
    distB = readCm(TB_TRIG, TB_ECHO);
    winB[winIdx] = (distB >= 2 && distB <= DETECT_CM);
    winIdx = (winIdx + 1) % WINDOW;                       // advance after both sampled
  }
}

void changePhase(Phase p, unsigned long now) {
  phase = p;
  phaseStart = now;
  if (p == A_GREEN || p == B_GREEN) {
    greenLen = BASE_GREEN_MS;
    extended = false;
  }
  setTraffic(p);
}

void updateTraffic(unsigned long now) {
  unsigned long elapsed = now - phaseStart;
  int scoreA = countTrue(winA);
  int scoreB = countTrue(winB);

  switch (phase) {
    case A_GREEN:
      if (elapsed >= greenLen) {
        // extend once if A leads by LEAD_MIN or more (tie / small gap = standard timing)
        if (!extended && (scoreA - scoreB) >= LEAD_MIN) {
          greenLen = BASE_GREEN_MS + EXT_MS;
          extended = true;
        } else {
          changePhase(A_YELLOW, now);
        }
      }
      break;
    case A_YELLOW:
      if (elapsed >= YELLOW_MS) changePhase(B_GREEN, now);
      break;
    case B_GREEN:
      if (elapsed >= greenLen) {
        if (!extended && (scoreB - scoreA) >= LEAD_MIN) {
          greenLen = BASE_GREEN_MS + EXT_MS;
          extended = true;
        } else {
          changePhase(B_YELLOW, now);
        }
      }
      break;
    case B_YELLOW:
      if (elapsed >= YELLOW_MS) changePhase(A_GREEN, now);
      break;
  }
}

// =====================================================
// Serial Communication Module
// =====================================================
const char* phaseName(Phase p) {
  switch (p) {
    case A_GREEN:  return "A_GREEN";
    case A_YELLOW: return "A_YELLOW";
    case B_GREEN:  return "B_GREEN";
    default:       return "B_YELLOW";
  }
}

void report(unsigned long now) {
  String sl = String("SL,ldr=") + ldrValue + ",dark=" + (isDark ? 1 : 0) +
              ",led=" + (stlOn ? 1 : 0) + ",fault=" + (pirFault ? 1 : 0);
  String pk = String("PK,dist=") + parkDist + ",state=" + (parkOccupied ? "OCC" : "FREE");
  String tr = String("TR,phase=") + phaseName(phase) + ",a=" + countTrue(winA) +
              ",b=" + countTrue(winB) + ",ext=" + (extended ? 1 : 0);

  bool beat = (now - lastHeartbeat >= HEARTBEAT_MS);
  // send on change, or all lines on heartbeat (resync after reconnect)
  if (beat || sl != lastSL) { Serial.println(sl); lastSL = sl; }
  if (beat || pk != lastPK) { Serial.println(pk); lastPK = pk; }
  if (beat || tr != lastTR) { Serial.println(tr); lastTR = tr; }
  if (beat) lastHeartbeat = now;
}

// Optional command: "T=350" sets dark threshold (50..900); band stays +100
void handleSerial() {
  static String buf;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (buf.startsWith("T=")) {
        int v = buf.substring(2).toInt();
        if (v >= 50 && v <= 900) {
          DARK_ON = v;
          DARK_OFF = v + 100;
          Serial.print("ACK,T=");
          Serial.println(v);
        } else {
          Serial.println("ERR,T range 50-900");
        }
      }
      buf = "";
    } else if (buf.length() < 16) {
      buf += c;
    }
  }
}

// =====================================================
// Arduino entry points
// =====================================================
void setup() {
  Serial.begin(9600);

  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_STL_LED, OUTPUT);

  pinMode(PK_TRIG, OUTPUT); pinMode(PK_ECHO, INPUT);
  pinMode(PK_GREEN, OUTPUT); pinMode(PK_RED, OUTPUT);

  pinMode(TA_TRIG, OUTPUT); pinMode(TA_ECHO, INPUT);
  pinMode(TB_TRIG, OUTPUT); pinMode(TB_ECHO, INPUT);
  pinMode(A_R, OUTPUT); pinMode(A_Y, OUTPUT); pinMode(A_G, OUTPUT);
  pinMode(B_R, OUTPUT); pinMode(B_Y, OUTPUT); pinMode(B_G, OUTPUT);

  for (int i = 0; i < WINDOW; i++) { winA[i] = false; winB[i] = false; }

  // fail-safe start state
  digitalWrite(PIN_STL_LED, LOW);
  digitalWrite(PK_GREEN, HIGH);
  digitalWrite(PK_RED, LOW);
  changePhase(A_GREEN, millis());

  Serial.println("BOOT,smart_city_v1");
}

void loop() {
  static unsigned long lastSense = 0;
  static uint8_t slot = 0;
  unsigned long now = millis();

  updateStreetlight(now);
  handleSerial();

  // one ultrasonic read per 60 ms, round-robin (avoids cross-talk between sensors)
  if (now - lastSense >= 60) {
    lastSense = now;
    switch (slot) {
      case 0: sampleTraffic(true);  break;
      case 1: sampleTraffic(false); break;
      case 2: updateParking();      break;
    }
    slot = (slot + 1) % 3;
  }

  updateTraffic(now);
  report(now);
}
