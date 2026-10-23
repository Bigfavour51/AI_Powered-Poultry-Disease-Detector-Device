// pi_link.h - main-board side of the Raspberry Pi UART link (Arduino, PlatformIO).
// Builds for the ESP32-S3 main board and for the Blue Pill (STM32duino) test sketch.
//
// Two copies must stay identical: raspberry-pi/stm32/pi_link.h (next to the Pi code) and
// main-board/lib/PiLink/pi_link.h (the main-board firmware).
//
// Wiring (both sides are 3.3 V logic, no level shifter needed):
//   ESP32-S3 GPIO17 (UART1 TX) / Blue Pill PA2 (USART2 TX)  ->  Pi GPIO15 RXD (pin 10)
//   ESP32-S3 GPIO18 (UART1 RX) / Blue Pill PA3 (USART2 RX)  <-  Pi GPIO14 TXD (pin 8)
//   GND                                                     --  Pi GND (pin 6)
//
// Usage (ESP32):
//   #include "pi_link.h"
//   HardwareSerial PiSerial(1);          // UART1
//   PiLink pi(PiSerial);
//   void setup() { pi.begin(18, 17); }   // RX, TX
// Usage (STM32): HardwareSerial PiSerial(PA3, PA2); ... pi.begin();
// Then on either board:
//   void loop() {
//     if (pi.update()) { /* new pi.result: show on CYD, log to SD, send over 4G */ }
//     bool piOk = pi.piAlive();          // "RASPBERRYPI" status dot on the dashboard
//   }
//   pi.requestCapture();                 // e.g. when a remote command arrives over 4G
//
// Frame format: $PDD,<TYPE>,<fields>*<CS>\r\n  (CS = XOR of bytes between '$' and '*')
// After each RES the Pi sends a small JPEG of the photo as IMG + IMD frames (base64). It is kept
// in pi.image when PI_LINK_IMAGE_MAX > 0 (ESP32 default); smaller boards ignore those frames.
#pragma once
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

#ifndef PI_LINK_IMAGE_MAX
#if defined(ARDUINO_ARCH_ESP32)
#define PI_LINK_IMAGE_MAX 8192
#else
#define PI_LINK_IMAGE_MAX 0
#endif
#endif

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

#if defined(ARDUINO_ARCH_ESP32)
  // ESP32 UARTs can use any GPIO, so the pins are given here
  void begin(int rxPin, int txPin, uint32_t baud = 115200) {
    s_.begin(baud, SERIAL_8N1, rxPin, txPin);
  }
#else
  void begin(uint32_t baud = 115200) { s_.begin(baud); }
#endif

  // Call every pass of loop(). Returns true when a new detection result has arrived.
  bool update() {
    bool gotResult = false;
    while (s_.available()) {
      char c = (char)s_.read();
      if (c == '\r') continue;
      if (c == '\n') {
        line_[len_] = '\0';
        if (rawLog && len_ > 0) { rawLog->print("PI> "); rawLog->println(line_); }
        if (len_ > 0 && handleLine(line_)) gotResult = true;
        len_ = 0;
      } else if (len_ < sizeof(line_) - 1) {
        line_[len_++] = c;
      } else {
        len_ = 0;  // overlong line: discard
      }
    }
    if (busy && !piAlive()) busy = false;  // Pi went silent mid-capture: no result is coming
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
  uint32_t pongs = 0;         // PONG replies received (answers to ping())
  Print *rawLog = nullptr;    // set to e.g. &Serial to log every frame: "PI> " in, "PI< " out

#if PI_LINK_IMAGE_MAX > 0
  // Photo of result imageSeq (JPEG, imageW x imageH). imageReady is set when a complete one has
  // arrived; clear it after using image[0..imageLen).
  uint8_t image[PI_LINK_IMAGE_MAX];
  size_t imageLen = 0;
  uint16_t imageSeq = 0, imageW = 0, imageH = 0;
  bool imageReady = false;
#endif

 private:
  HardwareSerial &s_;
  char line_[112];
  size_t len_ = 0;
  uint32_t lastRxMs_ = 0;
#if PI_LINK_IMAGE_MAX > 0
  size_t imageExpect_ = 0, imageGot_ = 0;  // photo being received (0 = none)

  // Decodes base64 text into out (at most max bytes). Returns the byte count, or -1 on error.
  static int base64Decode(const char *in, uint8_t *out, size_t max) {
    uint32_t acc = 0;
    int bits = 0;
    size_t n = 0;
    for (; *in && *in != '='; in++) {
      char c = *in;
      int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26
            : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
      if (v < 0) return -1;
      acc = (acc << 6) | (uint32_t)v;
      bits += 6;
      if (bits >= 8) {
        bits -= 8;
        if (n >= max) return -1;
        out[n++] = (uint8_t)(acc >> bits);
      }
    }
    return (int)n;
  }
#endif

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
    if (rawLog) { rawLog->print("PI< "); rawLog->print(frame); }
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
    } else if (strcmp(type, "PONG") == 0) {
      pongs++;
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
#if PI_LINK_IMAGE_MAX > 0
    else if (strcmp(type, "IMG") == 0 && n >= 6) {
      imageSeq = (uint16_t)atoi(f[2]);
      imageW = (uint16_t)atoi(f[3]);
      imageH = (uint16_t)atoi(f[4]);
      imageExpect_ = strtoul(f[5], nullptr, 10);
      if (imageExpect_ > sizeof(image)) imageExpect_ = 0;  // too big to keep: skip this photo
      imageGot_ = 0;
      imageReady = false;
    } else if (strcmp(type, "IMD") == 0 && n >= 5) {
      // Chunks must arrive in order; a gap drops the photo
      if (imageExpect_ == 0 || (uint16_t)atoi(f[2]) != imageSeq ||
          strtoul(f[3], nullptr, 10) != imageGot_) {
        imageExpect_ = 0;
        return false;
      }
      int got = base64Decode(f[4], image + imageGot_, imageExpect_ - imageGot_);
      if (got < 0) { imageExpect_ = 0; badFrames++; return false; }
      imageGot_ += (size_t)got;
      if (imageGot_ == imageExpect_) {
        imageLen = imageGot_;
        imageExpect_ = 0;
        imageReady = true;
      }
    }
#endif
    return false;
  }
};
