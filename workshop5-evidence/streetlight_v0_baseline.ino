const int LDR_PIN = A0;   // LDR + 10k divider, higher reading = brighter
const int PIR_PIN = 2;    // PIR OUT (or push-button stand-in)
const int LED_PIN = 8;    // LED through 220 ohm
const int DARK_T  = 300;  // hard-coded threshold (ADC counts, 0-1023)

void setup() {
  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(9600);
}

void loop() {
  int ldr = analogRead(LDR_PIN);
  bool motion = (digitalRead(PIR_PIN) == HIGH);
  bool dark = (ldr < DARK_T);
  bool led = dark && motion;
  digitalWrite(LED_PIN, led ? HIGH : LOW);
  Serial.print("ldr="); Serial.print(ldr);
  Serial.print(",motion="); Serial.print(motion);
  Serial.print(",led="); Serial.println(led);
  delay(100);
}
