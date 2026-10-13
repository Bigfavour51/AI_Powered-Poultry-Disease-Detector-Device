#!/usr/bin/env python3
"""Button test: press the capture button and watch the presses being counted.

Wiring: push button between GPIO17 (physical pin 11) and GND (pin 9).
Run:    ./deploy.sh venv/bin/python tests/test_button.py
"""
import argparse
import time

from gpiozero import Button

ap = argparse.ArgumentParser()
ap.add_argument("--gpio", type=int, default=17)
ap.add_argument("--seconds", type=int, default=30)
args = ap.parse_args()

button = Button(args.gpio, pull_up=True, bounce_time=0.05)
count = 0
t0 = time.monotonic()


def pressed():
    global count
    count += 1
    print(f"  press #{count} at {time.monotonic() - t0:5.2f} s")


button.when_pressed = pressed
print(f"Watching GPIO{args.gpio} for {args.seconds} s. Press the button a few times...")
print(f"Current state: {'PRESSED' if button.is_pressed else 'released'}")
time.sleep(args.seconds)

if count:
    print(f"PASS: {count} press(es) detected")
else:
    print("FAIL: no presses. Check the button is on pin 11 and pin 9 (GND), and that it closes when pressed.")
