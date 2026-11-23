# Smart City wiring (Arduino Uno R3)

Every LED: pin -> 220 ohm resistor -> LED long leg (+), short leg -> GND.

| Module | Part | Arduino pin |
|---|---|---|
| Streetlight | PIR OUT | D2 (VCC 5V, GND) |
| Streetlight | LDR divider middle | A0 (5V - LDR - A0 - 10k - GND) |
| Streetlight | Light LED (white/yellow) | D8 |
| Parking | HC-SR04 TRIG / ECHO | D3 / D4 |
| Parking | Green LED (free) / Red LED (occupied) | D5 / D6 |
| Traffic | Sensor A TRIG / ECHO | D9 / D10 |
| Traffic | Sensor B TRIG / ECHO | D11 / D12 |
| Traffic | Direction A Red / Yellow / Green | A1 / A2 / A3 |
| Traffic | Direction B Red / Yellow / Green | A4 / A5 / D7 |

Power: all sensors VCC to 5V and GND to GND on the breadboard rails.
D0/D1 stay free (USB serial). D13 is spare.

LDR note: with this divider, a higher reading = brighter. If yours reads the opposite way, swap the LDR and the 10k.

## Build and test order
1. Streetlight only: cover the LDR, wave a hand; LED on only when dark AND motion, stays 10 s.
2. Parking: hand 5-10 cm from the sensor = red, far away = green.
3. Traffic: two sensors, watch the serial monitor (9600) for `TR,...` lines.
4. Then run `dashboard.py` with the Serial Monitor closed.
