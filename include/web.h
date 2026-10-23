// web.h - WiFi access point + live dashboard served by the S3.
//
// A phone joins the open "PoultryDetector" network (no password; see web.cpp); a captive-portal DNS
// sends every address to the S3, so the dashboard opens by itself (or browse to 192.168.4.1).
// Endpoints:
//   GET  /              the dashboard (src/web_page.h, everything inline: no internet needed)
//   GET  /api/state     live JSON: sensors, Pi status, the last detections
//   GET  /api/history   sensor history (one sample every 10 s, last 15 min)
//   GET  /photo?seq=N   JPEG of detection N (the last WEB_DETECTIONS are kept in PSRAM)
//   POST /api/capture   same as the console 'c' / the Pi's button
#pragma once
#include <Arduino.h>
#include "pi_link.h"
#include "cyd_link.h"

// display: the values the CYD shows (sensors + Pi status), read live by the handlers
void webBegin(PiLink &pi, const CydTelemetry &display);
void webLoop();                                   // call every pass of loop()
void webAddResult(const PiResult &r);             // a new detection arrived
void webAddPhoto(uint16_t seq, const uint8_t *jpeg, size_t len);
