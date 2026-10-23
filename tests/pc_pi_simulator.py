#!/usr/bin/env python3
"""Runs on your PC, not the Pi. Pretends to be the Raspberry Pi through a USB-TTL adapter (FT232R),
so the STM32 side of the link (stm32/pi_link.h) can be tested on a Blue Pill without the Pi.

Needs stm32/test_pi_link flashed to the Blue Pill (flashing steps are in its platformio.ini).
That sketch reports everything it parses as '# ...' lines; this script checks them.

Wiring (SET THE ADAPTER JUMPER TO 3.3 V):
    Blue Pill PA2 (USART2 TX)  ->  adapter RXD
    Blue Pill PA3 (USART2 RX)  <-  adapter TXD
    Blue Pill GND       --  adapter GND

Run:  python tests/pc_pi_simulator.py --port /dev/ttyUSB0      then press RESET on the Blue Pill.
      --quick skips the ~13 s Pi-silent (piAlive timeout) test.
"""
import argparse
import sys
import time
from pathlib import Path

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pdd_inference import BAUD, checksum, make_frame, parse_frame  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--port", default="/dev/ttyUSB0")
ap.add_argument("--quick", action="store_true", help="skip the piAlive timeout test")
args = ap.parse_args()

ser = serial.Serial(args.port, BAUD, timeout=0.05)
rx = b""
bad = 0          # last badFrames count reported by the STM32
passed, failed = 0, 0


def lines(timeout):
    """Yield lines from the STM32 until `timeout` seconds pass, and None after every read so
    callers can stop early. Frames come back as 'FRAME ...'."""
    global rx, bad
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        rx += ser.read(256)
        yield None
        while b"\n" in rx:
            raw, rx = rx.split(b"\n", 1)
            text = raw.decode("ascii", "replace").strip()
            if not text:
                continue
            if text.startswith("$"):
                fields = parse_frame(text)
                text = f"FRAME {','.join(fields)}" if fields else f"BADFRAME {text}"
            elif text.startswith("# BAD "):
                bad = int(text[6:])
            yield text


def check(name, send, want, forbid=(), timeout=1.5):
    """Send raw bytes (or a list of them), then wait for every line in `want`.
    Fails if a line starting with anything in `forbid` shows up."""
    global passed, failed
    for chunk in send if isinstance(send, list) else [send]:
        ser.write(chunk)
    missing, got, bad_lines = list(want), [], []
    end = time.monotonic() + timeout
    for line in lines(timeout):
        if line is not None:
            got.append(line)
            if line in missing:
                missing.remove(line)
            if any(line.startswith(f) for f in forbid) or line.startswith("BADFRAME"):
                bad_lines.append(line)
            if not missing:
                end = min(end, time.monotonic() + 0.2)  # linger to catch forbidden lines
        if time.monotonic() >= end:
            break
    if missing or bad_lines:
        failed += 1
        print(f"FAIL  {name}")
        for m in missing:
            print(f"        missing:    {m}")
        for b in bad_lines:
            print(f"        unexpected: {b}")
        print(f"        received:   {got}")
    else:
        passed += 1
        print(f"PASS  {name}")


def frame(*fields):
    return make_frame(*fields)


print(f"Listening on {args.port}. Press RESET on the Blue Pill...")
for line in lines(30):
    if line == "# READY":
        break
else:
    sys.exit("No '# READY' from the Blue Pill in 30 s. Check wiring (PA2 -> RXD, PA3 <- TXD, GND), "
             "BOOT0 back on 0, and that test_pi_link is flashed.")
print("Blue Pill ready.\n")

# --- normal traffic ------------------------------------------------------------------------
check("HB sets status and alive; STM32 answers with valid PING and CAP frames",
      frame("HB", 0, 1, 1, 5),
      ["# STATUS cam=1 model=1", "# ALIVE 1", "FRAME PING", "FRAME CAP"])
check("BOOT updates camera/model flags",
      frame("BOOT", "1.0", 1, 0), ["# STATUS cam=1 model=0"])
check("BOOT with both ok",
      frame("BOOT", "1.0", 1, 1), ["# STATUS cam=1 model=1"])
check("BUSY sets busy",
      frame("BUSY", 1, "REM"), ["# BUSY 1"])
check("RES (OK) parsed field by field and clears busy",
      frame("RES", 1, "COCCIDIOSIS", 87, 87, 6, 4, 3, "OK", 3, 412),
      ["# RES seq=1 label=COCCIDIOSIS conf=87 p=87,6,4,3 low=0 agree=3 ms=412", "# BUSY 0"])
# BUSY and its RES/ERR go in separate steps: sent together, one update() would set and clear
# busy before the sketch could report it (the real Pi sends them seconds apart).
check("BUSY from the button",
      frame("BUSY", 2, "BTN"), ["# BUSY 1"])
check("RES (LOW, long inference time)",
      frame("RES", 2, "SALMONELLA", 41, 20, 19, 20, 41, "LOW", 1, 10234),
      ["# RES seq=2 label=SALMONELLA conf=41 p=20,19,20,41 low=1 agree=1 ms=10234", "# BUSY 0"])
check("Longest realistic RES frame",
      frame("RES", 65535, "COCCIDIOSIS", 100, 100, 100, 100, 100, "LOW", 3, 65535),
      ["# RES seq=65535 label=COCCIDIOSIS conf=100 p=100,100,100,100 low=1 agree=3 ms=65535"])
check("BUSY before an error",
      frame("BUSY", 3, "REM"), ["# BUSY 1"])
check("ERR reported and clears busy",
      frame("ERR", 3, "CAM"), ["# ERR CAM", "# BUSY 0"])
check("BUSY before a second error",
      frame("BUSY", 4, "REM"), ["# BUSY 1"])
check("New ERR code shows",
      frame("ERR", 4, "IMG"), ["# ERR IMG", "# BUSY 0"])

slow = frame("RES", 5, "HEALTHY", 95, 1, 95, 2, 2, "OK", 3, 980)
check("Frame arriving in small pieces",
      [slow[i:i + 7] for i in range(0, len(slow), 7)],
      ["# RES seq=5 label=HEALTHY conf=95 p=1,95,2,2 low=0 agree=3 ms=980"])
check("PONG and HB are accepted silently",
      [frame("PONG"), frame("HB", 5, 1, 1, 60)], [], forbid=("# BAD",), timeout=0.5)

# --- corrupted input -----------------------------------------------------------------------
good = frame("RES", 6, "NEWCASTLE", 70, 10, 10, 70, 10, "OK", 2, 1500)
corrupt = good.replace(b"NEWCASTLE", b"NEWCASTLF")  # payload changed, checksum not
check("Bad checksum rejected and counted",
      corrupt, [f"# BAD {bad + 1}"], forbid=("# RES",))
check("Garbage line rejected and counted",
      b"hello stm32\r\n", [f"# BAD {bad + 1}"], forbid=("# RES",))
check("Over-long line discarded, link still works afterwards",
      [b"$" + b"A" * 300 + b"\r\n", good],
      [f"# BAD {bad + 1}",
       "# RES seq=6 label=NEWCASTLE conf=70 p=10,10,70,10 low=0 agree=2 ms=1500"])
check("Valid checksum but not a PDD frame rejected",
      f"$XYZ,GPS,1,2*{checksum('XYZ,GPS,1,2')}\r\n".encode(), [f"# BAD {bad + 1}"])

# --- Pi goes silent ------------------------------------------------------------------------
if not args.quick:
    print("\nPi-silent test: sending BUSY, then nothing for up to 15 s...")
    ser.write(frame("BUSY", 7, "REM"))
    seen = []
    for line in lines(15):
        if line is not None:
            seen.append(line)
        if "# ALIVE 0" in seen:
            break
    if "# ALIVE 0" in seen:
        passed += 1
        print("PASS  piAlive() goes false when the Pi stops talking")
    else:
        failed += 1
        print(f"FAIL  piAlive() did not go false within 15 s (received {seen})")
    if "# BUSY 0" in seen:
        passed += 1
        print("PASS  busy is cleared when the Pi goes silent mid-capture")
    else:
        failed += 1
        print("FAIL  busy is still set after the Pi went silent")
    check("Pi comes back: alive again, STM32 re-sends PING and CAP",
          frame("BOOT", "1.0", 1, 1), ["# ALIVE 1", "FRAME PING", "FRAME CAP"])

print(f"\n{passed} passed, {failed} failed")
sys.exit(1 if failed else 0)
