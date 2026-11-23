"""
Smart City dashboard - reads the Arduino serial lines and shows live status.
Usage:  pip install pyserial
        python dashboard.py COM3          (Windows)
        python dashboard.py /dev/ttyACM0  (Linux/Mac)
Also logs every line to smartcity_log.csv (timestamp, raw line).
"""
import csv
import queue
import sys
import threading
import time
import tkinter as tk

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM3"
BAUD = 9600

lines = queue.Queue()


def reader():
    while True:
        try:
            with serial.Serial(PORT, BAUD, timeout=1) as ser, \
                    open("smartcity_log.csv", "a", newline="") as f:
                w = csv.writer(f)
                time.sleep(2)  # Uno resets when the port opens
                while True:
                    raw = ser.readline().decode(errors="ignore").strip()
                    if raw:
                        w.writerow([time.time(), raw])
                        f.flush()
                        lines.put((time.time(), raw))
        except serial.SerialException:
            lines.put((time.time(), "ERR,disconnected - retrying"))
            time.sleep(2)  # auto-reconnect; Arduino heartbeat resyncs state


def parse(raw):
    parts = raw.split(",")
    tag, kv = parts[0], {}
    for p in parts[1:]:
        if "=" in p:
            k, v = p.split("=", 1)
            kv[k] = v
    return tag, kv


root = tk.Tk()
root.title("Smart City Dashboard - B5BC8 Terserah")
root.configure(padx=16, pady=12)
vars_ = {k: tk.StringVar(value="-") for k in ("sl", "pk", "tr", "lat", "status")}

for title, key in (("Streetlight", "sl"), ("Parking", "pk"),
                   ("Traffic light", "tr"), ("Last update age", "lat"),
                   ("Link", "status")):
    tk.Label(root, text=title, font=("Segoe UI", 11, "bold")).pack(anchor="w")
    tk.Label(root, textvariable=vars_[key], font=("Consolas", 12)).pack(anchor="w", pady=(0, 8))

last_seen = {"t": None}


def poll():
    try:
        while True:
            t, raw = lines.get_nowait()
            tag, kv = parse(raw)
            if tag == "SL":
                vars_["sl"].set(f"LDR {kv.get('ldr')}  dark={kv.get('dark')}  "
                                f"light={'ON' if kv.get('led') == '1' else 'OFF'}"
                                f"{'  PIR FAULT' if kv.get('fault') == '1' else ''}")
            elif tag == "PK":
                vars_["pk"].set(f"{kv.get('state')}  (dist {kv.get('dist')} cm)")
            elif tag == "TR":
                vars_["tr"].set(f"{kv.get('phase')}  score A={kv.get('a')} B={kv.get('b')}"
                                f"{'  (extended)' if kv.get('ext') == '1' else ''}")
            elif tag == "ERR":
                vars_["status"].set(raw)
            if tag in ("SL", "PK", "TR"):
                last_seen["t"] = t
                vars_["status"].set("connected")
    except queue.Empty:
        pass
    if last_seen["t"]:
        vars_["lat"].set(f"{time.time() - last_seen['t']:.1f} s ago")
    root.after(100, poll)


threading.Thread(target=reader, daemon=True).start()
poll()
root.mainloop()
