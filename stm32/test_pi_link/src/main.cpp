// test_pi_link - Blue Pill test sketch for pi_link.h.
//
// The PC plays the Raspberry Pi through the FT232R (tests/pc_pi_simulator.py). Every change this
// sketch sees in the PiLink state is reported back on the same UART as a "# ..." line, which the
// PC script checks. These lines are for testing only; the real firmware must not send them.
//
// Wiring (adapter jumper on 3.3 V): PA2 (TX) -> adapter RXD, PA3 (RX) <- adapter TXD, GND -- GND.
// Onboard LED (PC13) is lit while the Pi reports a capture in progress.
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../../pi_link.h"

HardwareSerial PiSerial(PA3, PA2);  // RX, TX (USART2, same pins as the main board)
PiLink pi(PiSerial);

static bool lastCam = false, lastModel = false, lastBusy = false, lastAlive = false;
static char lastErr[8] = "";
static uint32_t lastBad = 0;

static void report(const char *fmt, ...) {
  char buf[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  PiSerial.print("# ");
  PiSerial.print(buf);
  PiSerial.print("\r\n");
}

void setup() {
  pinMode(PC13, OUTPUT);
  digitalWrite(PC13, HIGH);  // LED off (active low)
  pi.begin();
  report("READY");
}

void loop() {
  if (pi.update()) {
    const PiResult &r = pi.result;
    report("RES seq=%u label=%s conf=%u p=%u,%u,%u,%u low=%d agree=%u ms=%u",
           (unsigned)r.seq, r.label, (unsigned)r.confidence, (unsigned)r.prob[0],
           (unsigned)r.prob[1], (unsigned)r.prob[2], (unsigned)r.prob[3], (int)r.lowConfidence,
           (unsigned)r.agree, (unsigned)r.inferenceMs);
  }
  if (pi.cameraOk != lastCam || pi.modelOk != lastModel) {
    lastCam = pi.cameraOk;
    lastModel = pi.modelOk;
    report("STATUS cam=%d model=%d", (int)lastCam, (int)lastModel);
  }
  if (pi.busy != lastBusy) {
    lastBusy = pi.busy;
    digitalWrite(PC13, lastBusy ? LOW : HIGH);
    report("BUSY %d", (int)lastBusy);
  }
  if (strcmp(pi.lastError, lastErr) != 0) {
    strcpy(lastErr, pi.lastError);
    if (lastErr[0]) report("ERR %s", lastErr);
  }
  if (pi.badFrames != lastBad) {
    lastBad = pi.badFrames;
    report("BAD %lu", (unsigned long)lastBad);
  }
  bool alive = pi.piAlive();
  if (alive != lastAlive) {
    lastAlive = alive;
    report("ALIVE %d", (int)alive);
    if (alive) {  // exercise the STM32 -> Pi direction whenever the Pi (re)appears
      pi.ping();
      pi.requestCapture();
    }
  }
}
