#define USE_CONTROL 0        // 0 = plain threshold (v1), 1 = hysteresis + averaging (v2)

const int LDR_PIN = A0;
const int PIR_PIN = 2;
const int LED_PIN = 8;

const int T_MIN = 50, T_MAX = 900;   // allowed threshold range (ADC counts)
const int HYST  = 40;                // v2: extra counts needed to leave "dark"
const int NOISE = 30;                // injected noise amplitude (counts)
const int N     = 5;                 // v2: moving-average window

int darkT = 300;                     // configurable threshold, default 300
bool noiseOn = false;                // test hook: software-injected noise
bool dark = false;
int buf[N]; int idx = 0; long sum = 0;
bool lastLed = false;
unsigned long changes = 0;

void setup() {
  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(9600);
  int first = analogRead(LDR_PIN);
  for (int i = 0; i < N; i++) { buf[i] = first; sum += first; }
  Serial.println("READY T=300");
}

void handleSerial() {
  if (!Serial.available()) return;
  String s = Serial.readStringUntil('\n');
  s.trim();
  if (s.startsWith("T=")) {
    long v = s.substring(2).toInt();   // text gives 0, which is out of range
    if (v >= T_MIN && v <= T_MAX) { darkT = (int)v; Serial.print("OK T="); Serial.println(darkT); }
    else Serial.println("ERR range 50-900");
  } else if (s == "N=1") { noiseOn = true;  Serial.println("OK N=1"); }
  else if (s == "N=0")   { noiseOn = false; Serial.println("OK N=0"); }
  else Serial.println("ERR unknown");
}

void loop() {
  handleSerial();
  int raw = analogRead(LDR_PIN);
  if (noiseOn) raw = constrain(raw + random(-NOISE, NOISE + 1), 0, 1023);
  bool motion = (digitalRead(PIR_PIN) == HIGH);

#if USE_CONTROL
  sum -= buf[idx]; buf[idx] = raw; sum += raw; idx = (idx + 1) % N;
  int value = sum / N;
  if (!dark && value < darkT) dark = true;
  else if (dark && value > darkT + HYST) dark = false;
#else
  int value = raw;
  dark = (value < darkT);
#endif

  bool led = dark && motion;
  digitalWrite(LED_PIN, led ? HIGH : LOW);
  if (led != lastLed) { changes++; lastLed = led; }

  Serial.print("ldr="); Serial.print(value);
  Serial.print(",T="); Serial.print(darkT);
  Serial.print(",motion="); Serial.print(motion);
  Serial.print(",led="); Serial.print(led);
  Serial.print(",changes="); Serial.println(changes);
  delay(100);
}
