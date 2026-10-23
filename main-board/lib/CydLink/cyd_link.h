// cyd_link.h - main board (ESP32-S3) -> CYD display UART link (Arduino, PlatformIO)
//
// Two copies must stay identical: main-board/lib/CydLink/cyd_link.h (main board, sends) and
// cyd-display/lib/CydLink/cyd_link.h (CYD, receives).
//
// Wiring (both sides are 3.3 V logic; GND must be shared):
//   S3 GPIO41 (UART2 TX)  ->  CYD GPIO35 (RX; input-only pin, fine for receiving)
//   S3 GPIO42 (UART2 RX)  <-  CYD GPIO27 (TX)
//
// 115200 8-N-1, one frame per line, same framing as the Pi link:
//   $CYD,TEL,<temp>,<hum>,<pres>,<nh3>,<batt>,<chg>,<disease>,<conf>,<seq>,<pi>,<cam>,<model>,
//        <busy>,<uptime>*<CS>\r\n                                   every 2 s and on every change
//     temp degC, hum %RH, pres hPa, nh3 ppm (1 decimal); batt %; chg 0/1;
//     disease COCCIDIOSIS|HEALTHY|NEWCASTLE|SALMONELLA; conf %; seq = Pi result number;
//     pi/cam/model = Pi alive, camera ok, AI model ok (0/1); busy = capture running (0/1);
//     uptime = main-board uptime in s. "-" = no value (sensor not fitted, no result yet).
//   $CYD,IMG,<seq>,<w>,<h>,<bytes>*<CS>        photo of result <seq> follows (baseline JPEG)
//   $CYD,IMD,<seq>,<offset>,<base64>*<CS>      one IMAGE_CHUNK-byte piece of it, in order
//   CS = XOR of bytes between '$' and '*'.
#pragma once
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

struct CydTelemetry {
  float temperature = NAN;  // NAN = no reading
  float humidity = NAN;
  float pressure = NAN;
  float ammonia = NAN;
  int battery = -1;         // %, -1 = unknown
  bool charging = false;
  char disease[16] = "";    // label from the Pi, "" = no result yet
  uint8_t confidence = 0;   // %
  uint16_t seq = 0;         // Pi result number the disease belongs to (0 = none)
  bool piAlive = false;
  bool cameraOk = false;
  bool modelOk = false;
  bool busy = false;        // capture/inference running on the Pi
  uint32_t uptime = 0;      // main board, s
};

namespace cyd {

const size_t FRAME_MAX = 128;     // longest frame incl. \r\n and the terminating 0
const size_t IMAGE_CHUNK = 48;   // photo bytes per IMD frame (64 base64 characters)

inline uint8_t xorSum(const char *p, size_t n) {
  uint8_t cs = 0;
  for (size_t i = 0; i < n; i++) cs ^= (uint8_t)p[i];
  return cs;
}

// Adds '$', the checksum and \r\n around body. Returns the frame length.
inline size_t wrap(const char *body, char *out, size_t n) {
  return snprintf(out, n, "$%s*%02X\r\n", body, xorSum(body, strlen(body)));
}

// Writes one TEL frame (with \r\n) into out (FRAME_MAX bytes). Returns its length.
inline size_t format(const CydTelemetry &t, char *out, size_t n) {
  const float v[4] = {t.temperature, t.humidity, t.pressure, t.ammonia};
  char f[4][12];
  for (int i = 0; i < 4; i++) {
    if (isnan(v[i])) strcpy(f[i], "-");
    else snprintf(f[i], sizeof(f[i]), "%.1f", v[i]);
  }
  char batt[8];
  if (t.battery < 0) strcpy(batt, "-");
  else snprintf(batt, sizeof(batt), "%d", t.battery);
  char body[FRAME_MAX];
  snprintf(body, sizeof(body), "CYD,TEL,%s,%s,%s,%s,%s,%d,%s,%u,%u,%d,%d,%d,%d,%lu", f[0], f[1],
           f[2], f[3], batt, t.charging ? 1 : 0, t.disease[0] ? t.disease : "-", t.confidence,
           t.seq, t.piAlive ? 1 : 0, t.cameraOk ? 1 : 0, t.modelOk ? 1 : 0, t.busy ? 1 : 0,
           (unsigned long)t.uptime);
  return wrap(body, out, n);
}

// Photo frames (sender side). data/len of formatImageChunk: at most IMAGE_CHUNK bytes.
inline size_t formatImageHeader(uint16_t seq, uint16_t w, uint16_t h, size_t bytes, char *out,
                                size_t n) {
  char body[48];
  snprintf(body, sizeof(body), "CYD,IMG,%u,%u,%u,%lu", seq, w, h, (unsigned long)bytes);
  return wrap(body, out, n);
}

inline size_t formatImageChunk(uint16_t seq, size_t offset, const uint8_t *data, size_t len,
                               char *out, size_t n) {
  static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char body[FRAME_MAX];
  size_t p = snprintf(body, sizeof(body), "CYD,IMD,%u,%lu,", seq, (unsigned long)offset);
  for (size_t i = 0; i < len; i += 3) {
    uint32_t v = (uint32_t)data[i] << 16;
    if (i + 1 < len) v |= (uint32_t)data[i + 1] << 8;
    if (i + 2 < len) v |= data[i + 2];
    body[p++] = B64[(v >> 18) & 63];
    body[p++] = B64[(v >> 12) & 63];
    body[p++] = i + 1 < len ? B64[(v >> 6) & 63] : '=';
    body[p++] = i + 2 < len ? B64[v & 63] : '=';
  }
  body[p] = '\0';
  return wrap(body, out, n);
}

// Checks one received line (without \r\n; modified in place) and splits it at the commas.
// f[0] is the frame type ("TEL", "IMG", "IMD"). Returns the field count, or -1 if invalid.
inline int split(char *line, char **f, int maxFields) {
  if (line[0] != '$') return -1;
  char *star = strrchr(line, '*');
  if (!star || strlen(star) != 3) return -1;
  uint8_t want = (uint8_t)strtoul(star + 1, nullptr, 16);
  if (xorSum(line + 1, (size_t)(star - line - 1)) != want) return -1;
  *star = '\0';
  if (strncmp(line + 1, "CYD,", 4) != 0) return -1;
  int n = 0;
  for (char *tok = strtok(line + 5, ","); tok && n < maxFields; tok = strtok(nullptr, ","))
    f[n++] = tok;
  return n;
}

// Fills t from the fields of a TEL frame. Returns false (t untouched) if they do not fit.
inline bool parseTel(char **f, int n, CydTelemetry &t) {
  if (n != 15 || strcmp(f[0], "TEL") != 0) return false;
  float *v[4] = {&t.temperature, &t.humidity, &t.pressure, &t.ammonia};
  for (int i = 0; i < 4; i++) *v[i] = strcmp(f[1 + i], "-") == 0 ? NAN : (float)atof(f[1 + i]);
  t.battery = strcmp(f[5], "-") == 0 ? -1 : atoi(f[5]);
  t.charging = atoi(f[6]) != 0;
  if (strcmp(f[7], "-") == 0) {
    t.disease[0] = '\0';
  } else {
    strncpy(t.disease, f[7], sizeof(t.disease) - 1);
    t.disease[sizeof(t.disease) - 1] = '\0';
  }
  t.confidence = (uint8_t)atoi(f[8]);
  t.seq = (uint16_t)atoi(f[9]);
  t.piAlive = atoi(f[10]) != 0;
  t.cameraOk = atoi(f[11]) != 0;
  t.modelOk = atoi(f[12]) != 0;
  t.busy = atoi(f[13]) != 0;
  t.uptime = strtoul(f[14], nullptr, 10);
  return true;
}

// Convenience: check + split + parseTel for one line.
inline bool parse(char *line, CydTelemetry &t) {
  char *f[16];
  int n = split(line, f, 16);
  return n > 0 && parseTel(f, n, t);
}

// Receive side of the photo frames: feed it the split IMG/IMD fields.
class ImageRx {
 public:
  ImageRx(uint8_t *buf, size_t max) : buf_(buf), max_(max) {}

  // Returns true when this frame completed a photo: data()/len(), seq, w, h are then valid.
  bool feed(char **f, int n) {
    if (n == 5 && strcmp(f[0], "IMG") == 0) {
      seq = (uint16_t)atoi(f[1]);
      w = (uint16_t)atoi(f[2]);
      h = (uint16_t)atoi(f[3]);
      expect_ = strtoul(f[4], nullptr, 10);
      if (expect_ > max_) expect_ = 0;  // too big to keep: skip this photo
      got_ = 0;
      return false;
    }
    if (n != 4 || strcmp(f[0], "IMD") != 0) return false;
    // Chunks must arrive in order; a gap drops the photo
    if (expect_ == 0 || (uint16_t)atoi(f[1]) != seq || strtoul(f[2], nullptr, 10) != got_) {
      expect_ = 0;
      return false;
    }
    int k = base64Decode(f[3], buf_ + got_, expect_ - got_);
    if (k < 0) { expect_ = 0; return false; }
    got_ += (size_t)k;
    if (got_ < expect_) return false;
    len_ = got_;
    expect_ = 0;
    return true;
  }

  const uint8_t *data() const { return buf_; }
  size_t len() const { return len_; }
  uint16_t seq = 0, w = 0, h = 0;

 private:
  uint8_t *buf_;
  size_t max_;
  size_t expect_ = 0, got_ = 0, len_ = 0;

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
};

}  // namespace cyd
