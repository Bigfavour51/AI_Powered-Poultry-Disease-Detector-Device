// web.cpp - WiFi access point, captive-portal DNS and the dashboard's HTTP endpoints.
#include "web.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "web_page.h"

static const char *AP_SSID = "PoultryDetector";
static const char *AP_PASSWORD = nullptr;        // open network (or a WPA2 password, >= 8 chars)
static const IPAddress AP_IP(192, 168, 4, 1);

static const int WEB_DETECTIONS = 12;            // detections (with photo) kept for the page
static const size_t PHOTO_MAX = 8192;            // same limit as PI_LINK_IMAGE_MAX
static const int HISTORY_LEN = 90;               // sensor samples: 90 x 10 s = 15 min
static const uint32_t HISTORY_EVERY_MS = 10000;

struct Detection {
  uint16_t seq = 0;                // 0 = empty slot
  char label[16] = "";
  uint8_t confidence = 0;
  uint8_t prob[4] = {0};
  uint8_t agree = 0;
  bool lowConfidence = false;
  uint16_t inferenceMs = 0;
  uint32_t atS = 0;                // main-board uptime when it arrived
  uint8_t *jpeg = nullptr;         // PHOTO_MAX bytes in PSRAM
  size_t jpegLen = 0;
};

static WebServer server(80);
static DNSServer dns;
static PiLink *pi_;
static const CydTelemetry *tel_;

static Detection detections[WEB_DETECTIONS];     // ring, newest at detHead
static int detHead = -1;
static float histT[HISTORY_LEN], histH[HISTORY_LEN], histN[HISTORY_LEN];
static int histCount = 0, histHead = 0;          // ring: next write at histHead
static uint32_t lastHistMs = 0;

// ---------------------------------------------------------------- JSON helpers
static void addNum(String &s, float v, int decimals = 1) {
  if (isnan(v)) { s += "null"; return; }
  s += String(v, decimals);
}

static void addDetection(String &s, const Detection &d) {
  s += "{\"seq\":"; s += d.seq;
  s += ",\"label\":\""; s += d.label;
  s += "\",\"conf\":"; s += d.confidence;
  s += ",\"p\":[";
  for (int i = 0; i < 4; i++) { if (i) s += ','; s += d.prob[i]; }
  s += "],\"agree\":"; s += d.agree;
  s += ",\"ms\":"; s += d.inferenceMs;
  s += ",\"low\":"; s += d.lowConfidence ? 1 : 0;
  s += ",\"at\":"; s += d.atS;
  s += ",\"img\":"; s += d.jpegLen ? 1 : 0;
  s += '}';
}

// ---------------------------------------------------------------- handlers
static void handleIndex() {
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", INDEX_HTML);
}

static void handleState() {
  String s;
  s.reserve(2400);
  s += "{\"up\":"; s += millis() / 1000;
  s += ",\"pi\":"; s += pi_->piAlive() ? 1 : 0;
  s += ",\"cam\":"; s += pi_->cameraOk ? 1 : 0;
  s += ",\"model\":"; s += pi_->modelOk ? 1 : 0;
  s += ",\"busy\":"; s += pi_->busy ? 1 : 0;
  s += ",\"err\":\""; s += pi_->lastError;
  s += "\",\"t\":"; addNum(s, tel_->temperature);
  s += ",\"h\":"; addNum(s, tel_->humidity, 0);
  s += ",\"n\":"; addNum(s, tel_->ammonia);
  s += ",\"det\":[";
  for (int k = 0, first = 1; k < WEB_DETECTIONS; k++) {
    const Detection &d = detections[(detHead - k + WEB_DETECTIONS) % WEB_DETECTIONS];
    if (detHead < 0 || d.seq == 0) break;
    if (!first) s += ',';
    first = 0;
    addDetection(s, d);
  }
  s += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}

static void handleHistory() {
  String s;
  s.reserve(2400);
  s += "{\"every\":"; s += HISTORY_EVERY_MS / 1000;
  const float *series[3] = {histT, histH, histN};
  const char *keys[3] = {"t", "h", "n"};
  for (int k = 0; k < 3; k++) {
    s += ",\""; s += keys[k]; s += "\":[";
    for (int i = 0; i < histCount; i++) {   // oldest first
      if (i) s += ',';
      addNum(s, series[k][(histHead - histCount + i + HISTORY_LEN) % HISTORY_LEN]);
    }
    s += ']';
  }
  s += '}';
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}

static void handlePhoto() {
  uint16_t seq = (uint16_t)server.arg("seq").toInt();
  for (const Detection &d : detections) {
    if (d.seq == seq && d.jpegLen) {
      server.sendHeader("Cache-Control", "max-age=3600");
      server.send_P(200, "image/jpeg", (const char *)d.jpeg, d.jpegLen);
      return;
    }
  }
  server.send(404, "text/plain", "no photo");
}

static void handleCapture() {
  if (!pi_->piAlive()) { server.send(503, "application/json", "{\"ok\":0,\"why\":\"Pi offline\"}"); return; }
  if (pi_->busy) { server.send(409, "application/json", "{\"ok\":0,\"why\":\"busy\"}"); return; }
  Serial.println("[web] -> CAP");
  pi_->requestCapture();
  server.send(200, "application/json", "{\"ok\":1}");
}

// Anything else (phone connectivity checks, other hosts): send it to the dashboard, which makes
// phones show the "sign in to network" page with it.
static void handleOther() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

// ---------------------------------------------------------------- public
void webBegin(PiLink &pi, const CydTelemetry &display) {
  pi_ = &pi;
  tel_ = &display;
  for (Detection &d : detections) {
    d.jpeg = (uint8_t *)ps_malloc(PHOTO_MAX);
  }

  WiFi.onEvent([](WiFiEvent_t e, WiFiEventInfo_t info) {
    if (e == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
      const uint8_t *m = info.wifi_ap_staconnected.mac;
      Serial.printf("[web] phone joined %02x:%02x:%02x:%02x:%02x:%02x\r\n", m[0], m[1], m[2], m[3], m[4], m[5]);
    } else if (e == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
      Serial.println("[web] phone left");
    } else if (e == ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED) {
      Serial.println("[web] phone got its IP address");
    }
  });
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(AP_SSID, AP_PASSWORD, 6, 0, 4);
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  // Full power (~20 dBm) draws current spikes that dip the 3.3 V rail on this board (it also
  // feeds the CYD, MQ135 heater and relay); the WPA2 handshake then fails ("wrong password").
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  dns.start(53, "*", AP_IP);

  server.on("/", HTTP_GET, handleIndex);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/photo", HTTP_GET, handlePhoto);
  server.on("/api/capture", HTTP_POST, handleCapture);
  server.onNotFound(handleOther);
  server.begin();
  Serial.printf("[web] %s: WiFi \"%s\" (%s), http://%s/\r\n", ok ? "ready" : "AP FAILED", AP_SSID,
                AP_PASSWORD ? "WPA2" : "open, no password", AP_IP.toString().c_str());
}

void webLoop() {
  dns.processNextRequest();
  server.handleClient();
  if (millis() - lastHistMs >= HISTORY_EVERY_MS) {
    lastHistMs = millis();
    histT[histHead] = tel_->temperature;
    histH[histHead] = tel_->humidity;
    histN[histHead] = tel_->ammonia;
    histHead = (histHead + 1) % HISTORY_LEN;
    if (histCount < HISTORY_LEN) histCount++;
  }
}

void webAddResult(const PiResult &r) {
  detHead = (detHead + 1) % WEB_DETECTIONS;
  Detection &d = detections[detHead];
  d.seq = r.seq;
  strcpy(d.label, r.label);
  d.confidence = r.confidence;
  memcpy(d.prob, r.prob, sizeof(d.prob));
  d.agree = r.agree;
  d.lowConfidence = r.lowConfidence;
  d.inferenceMs = r.inferenceMs;
  d.atS = millis() / 1000;
  d.jpegLen = 0;
}

void webAddPhoto(uint16_t seq, const uint8_t *jpeg, size_t len) {
  if (detHead < 0) return;
  Detection &d = detections[detHead];
  if (d.seq != seq || !d.jpeg || len > PHOTO_MAX) return;
  memcpy(d.jpeg, jpeg, len);
  d.jpegLen = len;
}
