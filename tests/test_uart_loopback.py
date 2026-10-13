#!/usr/bin/env python3
"""UART loopback test: proves the Pi's serial port sends and receives correctly.

Wiring: a single jumper from GPIO14 TXD (pin 8) to GPIO15 RXD (pin 10). Nothing else.
Run:    ./deploy.sh venv/bin/python tests/test_uart_loopback.py

Every frame sent should come straight back, unchanged, with a valid checksum.
"""
import argparse
import sys
import time
from pathlib import Path

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pdd_inference import BAUD, make_frame, parse_frame  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--port", default="/dev/serial0")
ap.add_argument("--count", type=int, default=50)
args = ap.parse_args()

ser = serial.Serial(args.port, BAUD, timeout=0.5)
ser.reset_input_buffer()
ok = 0
t0 = time.perf_counter()
for i in range(1, args.count + 1):
    frame = make_frame("RES", i, "COCCIDIOSIS", 87, 87, 6, 4, 3, "OK", 3, 412)
    ser.write(frame)
    back = ser.readline()
    if back == frame and parse_frame(back.decode()) is not None:
        ok += 1
    elif i <= 3 or not back:
        print(f"  frame {i}: sent {frame!r} got {back!r}")
elapsed = time.perf_counter() - t0
ser.close()

print(f"{ok}/{args.count} frames returned intact in {elapsed:.2f} s")
if ok == args.count:
    print("PASS: UART transmit and receive both work at", BAUD, "baud")
elif ok == 0:
    print("FAIL: nothing came back. Check the jumper is on pin 8 and pin 10, and that")
    print("      /dev/serial0 -> ttyAMA0 (ls -l /dev/serial0) and the serial console is disabled.")
else:
    print("PARTIAL: some frames corrupted - check the jumper contact.")
