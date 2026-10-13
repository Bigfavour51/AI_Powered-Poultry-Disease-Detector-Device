// pi_link.h - STM32 side of the Raspberry Pi UART link (Arduino / STM32duino, PlatformIO)
//
// Wiring (both sides are 3.3 V logic, no level shifter needed):
//   STM32 PB6 (USART1 TX, remapped)  ->  Pi GPIO15 RXD (pin 10)
//   STM32 PB7 (USART1 RX, remapped)  <-  Pi GPIO14 TXD (pin 8)
//   STM32 GND                        --  Pi GND (pin 6)
//
// NOTE: PB6/PB7 are the remapped pins of USART1. USART1 can only use ONE pin pair at a
// time, and the schematic also puts the A7670E 4G module on PA9/PA10 (USART1 default).
// Check how your firmware drives the 4G module before using this as-is.
//
// Usage:
//   #include "pi_link.h"
//   HardwareSerial PiSerial(PB7, PB6);   // RX, TX
//   PiLink pi(PiSerial);
//   void setup() { pi.begin(); }
//   void loop() {
//     if (pi.update()) { /* new pi.result: show on CYD, log to SD, send over 4G */ }
//     bool piOk = pi.piAlive();          // "RASPBERRYPI" status dot on the dashboard
//   }
//   pi.requestCapture();                 // e.g. when a remote command arrives over 4G
//
// Frame format: $PDD,<TYPE>,<fields>*<CS>\r\n  (CS = XOR of bytes between '$' and '*')
#pragma once
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

struct PiResult {
  uint16_t seq = 0;
  char label[16] = "";      // COCCIDIOSIS, HEALTHY, NEWCASTLE, SALMONELLA
  uint8_t confidence = 0;   // %
  uint8_t prob[4] = {0};    // % for cocci, healthy, ncd, salmo
  bool lowConfidence = false;
  uint8_t agree = 0;        // models (out of 3) that picked the same class
  uint16_t inferenceMs = 0;
};

class PiLink {
 public:
  explicit PiLink(HardwareSerial &port) : s_(port) {}

  void begin(uint32_t baud = 115200) { s_.begin(baud); }

  // Call every pass of loop(). Returns true when a new detection result has arrived.
  bool update() {
    bool gotResult = false;
    while (s_.available()) {
      char c = (char)s_.read();
      if (c == '\r') continue;
      if (c == '\n') {
        line_[len_] = '\0';
        if (len_ > 0 && handleLine(line_)) gotResult = true;
        len_ = 0;
      } else if (len_ < sizeof(line_) - 1) {
        line_[len_++] = c;
      } else {
        len_ = 0;  // overlong line: discard
      }
    }
    return gotResult;
  }

  void requestCapture() { sendCmd("CAP"); }
  void ping() { sendCmd("PING"); }

  // Pi is considered alive if any valid frame arrived recently (heartbeat is every 5 s).
  bool piAlive(uint32_t timeoutMs = 12000) const {
    return lastRxMs_ != 0 && (millis() - lastRxMs_) < timeoutMs;
  }

  PiResult result;
  bool cameraOk = false;
  bool modelOk = false;
  bool busy = false;          // capture/inference in progress
  char lastError[8] = "";     // CAM, MODEL or IMG
  uint32_t badFrames = 0;     // checksum or format failures (useful for testing)

 private:
  HardwareSerial &s_;
  char line_[112];
  size_t len_ = 0;
  uint32_t lastRxMs_ = 0;

  static uint8_t xorSum(const char *p, size_t n) {
    uint8_t cs = 0;
    for (size_t i = 0; i < n; i++) cs ^= (uint8_t)p[i];
    return cs;
  }

  void sendCmd(const char *cmd) {
    char body[24];
    snprintf(body, sizeof(body), "PDD,%s", cmd);
    char frame[40];
    snprintf(frame, sizeof(frame), "$%s*%02X\r\n", body, xorSum(body, strlen(body)));
    s_.print(frame);
  }

  bool handleLine(char *line) {
    if (line[0] != '$') { badFrames++; return false; }
    char *star = strrchr(line, '*');
    if (!star || strlen(star) != 3) { badFrames++; return false; }
    uint8_t want = (uint8_t)strtoul(star + 1, nullptr, 16);
    if (xorSum(line + 1, (size_t)(star - line - 1)) != want) { badFrames++; return false; }
    *star = '\0';

    char *f[14];
    int n = 0;
    for (char *tok = strtok(line + 1, ","); tok && n < 14; tok = strtok(nullptr, ",")) f[n++] = tok;
    if (n < 2 || strcmp(f[0], "PDD") != 0) { badFrames++; return false; }
    lastRxMs_ = millis();
    if (lastRxMs_ == 0) lastRxMs_ = 1;
    const char *type = f[1];

    if (strcmp(type, "HB") == 0 && n >= 6) {
      cameraOk = atoi(f[3]);
      modelOk = atoi(f[4]);
    } else if (strcmp(type, "BOOT") == 0 && n >= 5) {
      cameraOk = atoi(f[3]);
      modelOk = atoi(f[4]);
      busy = false;
    } else if (strcmp(type, "BUSY") == 0) {
      busy = true;
      lastError[0] = '\0';
    } else if (strcmp(type, "ERR") == 0 && n >= 4) {
      busy = false;
      strncpy(lastError, f[3], sizeof(lastError) - 1);
      lastError[sizeof(lastError) - 1] = '\0';
    } else if (strcmp(type, "RES") == 0 && n >= 12) {
      result.seq = (uint16_t)atoi(f[2]);
      strncpy(result.label, f[3], sizeof(result.label) - 1);
      result.label[sizeof(result.label) - 1] = '\0';
      result.confidence = (uint8_t)atoi(f[4]);
      for (int i = 0; i < 4; i++) result.prob[i] = (uint8_t)atoi(f[5 + i]);
      result.lowConfidence = strcmp(f[9], "LOW") == 0;
      result.agree = (uint8_t)atoi(f[10]);
      result.inferenceMs = (uint16_t)atoi(f[11]);
      busy = false;
      return true;
    }
    return false;
  }
};
