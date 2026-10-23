#!/usr/bin/env python3
"""Runs on your PC, not the Pi. Pretends to be the STM32 through a USB-TTL adapter (FT232R),
so the Pi service can be tested end to end before the STM32 firmware exists.

Wiring (SET THE ADAPTER JUMPER TO 3.3 V - 5 V will damage the Pi):
    Pi pin 8  (GPIO14 TXD)  ->  adapter RXD
    Pi pin 10 (GPIO15 RXD)  <-  adapter TXD
    Pi pin 6  (GND)         --  adapter GND

On the Pi:   ./deploy.sh venv/bin/python pdd_inference.py -v
On the PC:   pip install pyserial
             python tests/pc_stm32_simulator.py --port /dev/ttyUSB0

Keys:  c + Enter  send CAP (remote capture)    p + Enter  send PING    q + Enter  quit
Every frame from the Pi is printed with its checksum status and decoded.
"""
import argparse
import sys
import threading
import time
from pathlib import Path

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pdd_inference import BAUD, CLASS_KEYS, make_frame, parse_frame  # noqa: E402


def describe(fields):
    kind = fields[0]
    if kind == "RES" and len(fields) >= 11:
        probs = ", ".join(f"{k} {p}%" for k, p in zip(CLASS_KEYS, fields[4:8]))
        return (f"RESULT #{fields[1]}: {fields[2]} {fields[3]}% [{fields[8]}] "
                f"agree {fields[9]}/3, {fields[10]} ms  ({probs})")
    if kind == "HB":
        return f"heartbeat seq={fields[1]} camera={fields[2]} model={fields[3]} uptime={fields[4]} s"
    if kind == "BOOT":
        return f"Pi service started v{fields[1]} camera={fields[2]} model={fields[3]}"
    if kind == "BUSY":
        return f"capture #{fields[1]} started ({'button' if fields[2] == 'BTN' else 'remote'})"
    if kind == "ERR":
        return f"ERROR #{fields[1]}: {fields[2]}"
    return " ".join(fields)


def reader(ser, stop):
    while not stop.is_set():
        line = ser.readline().decode("ascii", "ignore").strip()
        if not line:
            continue
        fields = parse_frame(line)
        stamp = time.strftime("%H:%M:%S")
        if fields is None:
            print(f"{stamp}  BAD FRAME (checksum/format): {line!r}")
        else:
            print(f"{stamp}  {describe(fields)}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyUSB0")
    args = ap.parse_args()
    ser = serial.Serial(args.port, BAUD, timeout=0.5)
    stop = threading.Event()
    threading.Thread(target=reader, args=(ser, stop), daemon=True).start()
    print(f"Listening on {args.port} at {BAUD} baud. c = capture, p = ping, q = quit")
    try:
        for cmd in sys.stdin:
            cmd = cmd.strip().lower()
            if cmd == "c":
                ser.write(make_frame("CAP"))
            elif cmd == "p":
                ser.write(make_frame("PING"))
            elif cmd == "q":
                break
    except KeyboardInterrupt:
        pass
    stop.set()
    ser.close()


if __name__ == "__main__":
    main()
