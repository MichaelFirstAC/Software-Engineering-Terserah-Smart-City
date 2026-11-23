"""Host-side simulation of the Workshop 5 streetlight sketches.
Mirrors streetlight_v0_baseline.ino and streetlight_v1_v2.ino line by line
(100 ms loop, integer maths, random(-30,31) noise). NOT hardware output."""
import random

T_MIN, T_MAX, HYST, NOISE, N = 50, 900, 40, 30, 5

class Sketch:
    def __init__(self, version, first_ldr, rng):
        self.v = version              # "v0", "v1" (USE_CONTROL 0), "v2" (USE_CONTROL 1)
        self.rng = rng
        self.darkT = 300
        self.noiseOn = False
        self.dark = False
        self.buf = [first_ldr] * N
        self.sum = first_ldr * N
        self.idx = 0
        self.lastLed = False
        self.changes = 0
        self.log = []
        if version != "v0":
            self.log.append("READY T=300")

    def cmd(self, s):                 # handleSerial()
        s = s.strip()
        if s.startswith("T="):
            txt = s[2:]
            import re                     # Arduino toInt(): leading integer, else 0
            m = re.match(r"\s*([+-]?\d+)", txt)
            v = int(m.group(1)) if m else 0
            if T_MIN <= v <= T_MAX:
                self.darkT = v; r = f"OK T={v}"
            else:
                r = "ERR range 50-900"
        elif s == "N=1": self.noiseOn = True; r = "OK N=1"
        elif s == "N=0": self.noiseOn = False; r = "OK N=0"
        else: r = "ERR unknown"
        self.log.append(f"> {s}  ->  {r}")
        return r

    def step(self, ldr, motion):      # one loop() pass
        raw = ldr
        if self.noiseOn:
            raw = max(0, min(1023, raw + self.rng.randint(-NOISE, NOISE)))
        if self.v == "v0":
            value = raw; self.dark = value < 300
        elif self.v == "v1":
            value = raw; self.dark = value < self.darkT
        else:
            self.sum -= self.buf[self.idx]; self.buf[self.idx] = raw
            self.sum += raw; self.idx = (self.idx + 1) % N
            value = self.sum // N
            if not self.dark and value < self.darkT: self.dark = True
            elif self.dark and value > self.darkT + HYST: self.dark = False
        led = self.dark and motion
        if led != self.lastLed:
            self.changes += 1; self.lastLed = led
        line = f"ldr={value},T={self.darkT},motion={int(motion)},led={int(led)},changes={self.changes}"
        self.log.append(line)
        return led

def settle(sk, ldr, motion=True, n=10):
    led = None
    for _ in range(n): led = sk.step(ldr, motion)
    return led

def section(t): print(f"\n===== {t} =====")

# ---------- EV-01 baseline smoke checks (v0) ----------
section("EV-01 baseline v0")
sk = Sketch("v0", 150, random.Random(1))
print("BS-01 LDR=150 motion=1 ->", "LED ON" if settle(sk,150) else "LED OFF")
print("BS-02 LDR=800 motion=1 ->", "LED ON" if settle(sk,800) else "LED OFF")
print("BS-02 LDR=299 motion=1 ->", "LED ON" if settle(sk,299) else "LED OFF")
print("BS-02 LDR=300 motion=1 ->", "LED ON" if settle(sk,300) else "LED OFF")
print("BS-01 LDR=150 motion=0 ->", "LED ON" if settle(sk,150,False) else "LED OFF")

# ---------- Part A checks on v1 ----------
section("EV-02 Part A (v1, USE_CONTROL 0)")
sk = Sketch("v1", 400, random.Random(1))
print("LDR=400 default T=300 ->", "ON" if settle(sk,400) else "OFF")
sk.cmd("T=500"); print("LDR=400 after T=500    ->", "ON" if settle(sk,400) else "OFF")
sk = Sketch("v1", 150, random.Random(1))
print("PA-01 LDR=150 ->", "ON" if settle(sk,150) else "OFF", "| LDR=800 ->", "ON" if settle(sk,800) else "OFF")
sk = Sketch("v1", 300, random.Random(1))
for c in ["T=abc","T=1500","T=-5","T=","T=500abc","T=49","T=50","T=900","T=901","hello"]:
    sk.cmd(c)
    if c in ("T=500abc","T=50","T=900"): sk.darkT = 300; 
print("--- boundary on v1, T reset to 300")
sk = Sketch("v1", 299, random.Random(1))
print("LDR=299 ->", "ON" if settle(sk,299) else "OFF", "| LDR=301 ->", "ON" if settle(sk,301) else "OFF")

# ---------- EV-03 TC-04 injection ----------
section("EV-03 TC-04 injection, LDR held 300, motion, N=1, 30 s = 300 loops")
res = {}
for ver in ("v1","v2"):
    totals = []
    for seed in range(1, 21):
        sk = Sketch(ver, 300, random.Random(seed))
        sk.cmd("N=1")
        for _ in range(300): sk.step(300, True)
        totals.append(sk.changes)
    res[ver] = totals
    print(f"{ver}: changes in 30 s over 20 seeds -> min {min(totals)}, max {max(totals)}, mean {sum(totals)/20:.1f}; seed1 = {totals[0]}")
sk = Sketch("v1", 300, random.Random(1)); sk.cmd("N=1")
for _ in range(300): sk.step(300, True)
print("v1 sample log (first 12 lines, seed 1):"); print("\n".join(sk.log[:12]))
sk = Sketch("v2", 300, random.Random(1)); sk.cmd("N=1")
for _ in range(300): sk.step(300, True)
print("v2 sample log (first 12 lines, seed 1):"); print("\n".join(sk.log[:12]))
print("v2 last line:", sk.log[-1])

# ---------- EV-04 TC-03 / TC-05 on v2 ----------
section("EV-04 TC-03 invalid input and boundary (v2), TC-05 recovery")
sk = Sketch("v2", 300, random.Random(1))
for c in ["T=abc","T=1500","T=-5","T=","T=49","T=901","hello"]: sk.cmd(c)
print("\n".join(l for l in sk.log if l.startswith(">")))
print("threshold after invalid commands:", sk.darkT)
sk.cmd("T=500abc"); print("residual limitation: T=500abc ->", sk.darkT); sk.darkT = 300
print("v2 LDR=299 ->", "ON" if settle(sk,299) else "OFF")
print("v2 LDR=301 ->", "ON" if settle(sk,301) else "OFF", "(hysteresis band 300-340: stays ON)")
print("v2 LDR=341 ->", "ON" if settle(sk,341) else "OFF")
sk = Sketch("v2", 300, random.Random(2)); sk.cmd("N=1")
for _ in range(100): sk.step(300, True)
sk.cmd("N=0")
loops = 0
while True:
    loops += 1
    if not sk.step(800, True): break
print(f"TC-05 LDR 300->800 after N=0: LED OFF after {loops} loop(s) = {loops*100} ms")
sk2 = Sketch("v2", 800, random.Random(2))
print("after reset T =", sk2.darkT, "(not persisted)")
