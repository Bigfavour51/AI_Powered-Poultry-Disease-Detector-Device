// Poultry Disease Detector - main board (ESP32-S3-DevKitC-1 N16R8)
//
// Links to the Raspberry Pi and the CYD display and reads the DHT11 and MQ135. Everything the Pi reports is
// logged on the debug port (UART0 via the board's CH343 "UART" USB port, `pio device monitor`)
// and shown on the CYD (a frame every 2 s and at once on any change) together with the sensor
// readings; the photo of each result is passed on to the CYD as well. The same data is served
// as a phone dashboard on the S3's own WiFi access point (web.cpp).
// Console commands (type a letter, then Enter):
//   c  ask the Pi for a capture      p  ping the Pi      s  link + sensor status
//   r  raw frame log on/off          g  calibrate the gas sensor (in clean air)   ?  help
#include <Arduino.h>
#include <DHT.h>
#include <Preferences.h>
#include "pins.h"
#include "pi_link.h"
#include "cyd_link.h"
#include "web.h"

HardwareSerial PiSerial(1);   // UART1, pins set in pi.begin()
PiLink pi(PiSerial);
HardwareSerial CydSerial(2);  // UART2
DHT dht(PIN_DHT, DHT11);

static CydTelemetry display;  // what the CYD shows; NAN = no reading (sensor missing or failed)
static bool shownDhtOk = true;

// MQ135: Rs (sensor resistance, in units of the module's load resistor, which cancels out of
// Rs/R0) from the AO divider; NH3 estimate from the datasheet curve ppm = 102.2 * (Rs/R0)^-2.473.
// R0 = Rs in clean air / 3.6. Set with 'g' (saved in flash), else taken once after warm-up.
static const float GAS_SUPPLY_MV = 3300.0f;      // module VCC
static const float GAS_CLEAN_AIR_RATIO = 3.6f;   // Rs/R0 in clean air (datasheet)
static const uint32_t GAS_WARMUP_MS = 60000;
static Preferences prefs;
static float gasR0 = NAN;    // NAN = not calibrated
static bool gasR0Saved = false;
static float gasMv = NAN, gasRs = NAN;
static bool shownGasOk = true;
static uint32_t lastDisplayMs = 0;

static const char *const CLASS_KEYS[4] = {"cocci", "healthy", "ncd", "salmo"};

// Last state printed, so only changes get logged
static bool shownCam = false, shownModel = false, shownBusy = false, shownAlive = false;
static char shownErr[8] = "";
static uint32_t shownBad = 0, shownPongs = 0;

static void printResult(const PiResult &r) {
  Serial.printf("[pi] RESULT #%u: %s %u%% %s, %u/3 models agree, %u ms\r\n", r.seq, r.label,
                r.confidence, r.lowConfidence ? "(LOW confidence)" : "", r.agree, r.inferenceMs);
  Serial.printf("     %s %u%%  %s %u%%  %s %u%%  %s %u%%\r\n", CLASS_KEYS[0], r.prob[0],
                CLASS_KEYS[1], r.prob[1], CLASS_KEYS[2], r.prob[2], CLASS_KEYS[3], r.prob[3]);
}

static void printStatus() {
  Serial.printf("[link] Pi %s, camera %s, model %s, %s, last error '%s', bad frames %lu, "
                "pongs %lu\r\n",
                pi.piAlive() ? "ALIVE" : "SILENT", pi.cameraOk ? "ok" : "FAIL",
                pi.modelOk ? "ok" : "FAIL", pi.busy ? "busy" : "idle", pi.lastError,
                (unsigned long)pi.badFrames, (unsigned long)pi.pongs);
  if (pi.result.seq) printResult(pi.result);
  Serial.printf("[dht] %.1f C, %.0f %%RH\r\n", display.temperature, display.humidity);
  Serial.printf("[gas] AO %.0f mV, Rs/R0 %.2f, NH3 ~%.1f ppm (R0 %s)\r\n", gasMv, gasRs / gasR0,
                display.ammonia, gasR0Saved ? "calibrated" : isnan(gasR0) ? "warming up" : "auto");
}

static void printHelp() {
  Serial.println("Commands: c = capture, p = ping, s = status, r = raw frame log on/off, "
                 "g = calibrate gas sensor in clean air");
}

// DHT11: at most one reading per second; a failed read gives NAN, shown as "--" on the CYD.
static void readDht() {
  display.temperature = dht.readTemperature();
  display.humidity = dht.readHumidity();
  bool ok = !isnan(display.temperature) && !isnan(display.humidity);
  if (ok != shownDhtOk) {
    shownDhtOk = ok;
    if (ok) Serial.printf("[dht] ok: %.1f C, %.0f %%RH\r\n", display.temperature, display.humidity);
    else Serial.println("[dht] no reading (check wiring on GPIO13 and 3.3 V)");
  }
}

static void readGas() {
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(PIN_GAS);
  gasMv = sum / 16.0f;
  bool ok = gasMv >= 5.0f;  // ~0 V: AO not connected
  if (ok != shownGasOk) {
    shownGasOk = ok;
    Serial.println(ok ? "[gas] ok" : "[gas] no signal on AO (check wiring on GPIO7 and 3.3 V)");
  }
  if (!ok) {
    gasRs = display.ammonia = NAN;
    return;
  }
  gasRs = (GAS_SUPPLY_MV - min(gasMv, GAS_SUPPLY_MV - 1.0f)) / gasMv;
  if (isnan(gasR0) && millis() >= GAS_WARMUP_MS) {
    gasR0 = gasRs / GAS_CLEAN_AIR_RATIO;
    Serial.println("[gas] auto-calibrated, assuming clean air now (type g in clean air to save)");
  }
  display.ammonia = isnan(gasR0) ? NAN : min(102.2f * powf(gasRs / gasR0, -2.473f), 999.0f);
}

static void calibrateGas() {
  if (isnan(gasRs)) {
    Serial.println("[gas] no signal, not calibrated");
    return;
  }
  gasR0 = gasRs / GAS_CLEAN_AIR_RATIO;
  gasR0Saved = true;
  prefs.putFloat("gasR0", gasR0);
  Serial.printf("[gas] calibrated and saved (AO %.0f mV in clean air)\r\n", gasMv);
}

static void sendDisplay() {
  display.seq = pi.result.seq;
  display.piAlive = pi.piAlive();
  display.cameraOk = pi.cameraOk;
  display.modelOk = pi.modelOk;
  display.busy = pi.busy;
  display.uptime = millis() / 1000;
  char frame[cyd::FRAME_MAX];
  cyd::format(display, frame, sizeof(frame));
  CydSerial.print(frame);
  if (pi.rawLog) { pi.rawLog->print("CYD< "); pi.rawLog->print(frame); }
  lastDisplayMs = millis();
}

// Passes the photo of the last result from the Pi on to the CYD (~90 frames, sent from the
// UART TX buffer in the background).
static void sendPhoto() {
  char frame[cyd::FRAME_MAX];
  cyd::formatImageHeader(pi.imageSeq, pi.imageW, pi.imageH, pi.imageLen, frame, sizeof(frame));
  CydSerial.print(frame);
  for (size_t off = 0; off < pi.imageLen; off += cyd::IMAGE_CHUNK) {
    size_t n = min(cyd::IMAGE_CHUNK, pi.imageLen - off);
    cyd::formatImageChunk(pi.imageSeq, off, pi.image + off, n, frame, sizeof(frame));
    CydSerial.print(frame);
  }
  Serial.printf("[cyd] photo #%u sent (%ux%u, %u bytes)\r\n", pi.imageSeq, pi.imageW, pi.imageH,
                (unsigned)pi.imageLen);
}

static void handleConsole() {
  while (Serial.available()) {
    switch (Serial.read()) {
      case 'c': Serial.println("[link] -> CAP"); pi.requestCapture(); break;
      case 'p': Serial.println("[link] -> PING"); pi.ping(); break;
      case 's': printStatus(); break;
      case 'r':
        pi.rawLog = pi.rawLog ? nullptr : &Serial;
        Serial.printf("[link] raw frame log %s\r\n", pi.rawLog ? "on" : "off");
        break;
      case 'g': calibrateGas(); sendDisplay(); break;
      case '?': case 'h': printHelp(); break;
      default: break;  // Enter, spaces, etc.
    }
  }
}

// Logs what changed on the Pi side. Returns true if the CYD should be updated now.
static bool logLinkChanges() {
  bool changed = false;
  bool alive = pi.piAlive();
  if (alive != shownAlive) {
    shownAlive = alive;
    changed = true;
    Serial.println(alive ? "[pi] link up" : "[pi] link LOST (no frames for 12 s)");
  }
  if (pi.cameraOk != shownCam || pi.modelOk != shownModel) {
    shownCam = pi.cameraOk;
    shownModel = pi.modelOk;
    changed = true;
    Serial.printf("[pi] camera %s, model %s\r\n", shownCam ? "ok" : "FAIL",
                  shownModel ? "ok" : "FAIL");
  }
  if (pi.busy != shownBusy) {
    shownBusy = pi.busy;
    changed = true;
    digitalWrite(PIN_LED, shownBusy ? HIGH : LOW);
    if (shownBusy) Serial.println("[pi] capture started...");
  }
  if (strcmp(pi.lastError, shownErr) != 0) {
    strcpy(shownErr, pi.lastError);
    if (shownErr[0]) Serial.printf("[pi] ERROR %s\r\n", shownErr);
  }
  if (pi.badFrames != shownBad) {
    shownBad = pi.badFrames;
    Serial.printf("[link] bad frame (total %lu)\r\n", (unsigned long)shownBad);
  }
  if (pi.pongs != shownPongs) {
    shownPongs = pi.pongs;
    Serial.println("[pi] PONG");
  }
  return changed;
}

void setup() {
  // Relay off first thing (output level set before the pin becomes an output, so no glitch)
  digitalWrite(PIN_RELAY, RELAY_OFF);
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);  // off
  Serial.begin(115200);
  PiSerial.setRxBufferSize(2048);   // photo frames arrive back to back
  pi.begin(PIN_PI_RX, PIN_PI_TX);
  CydSerial.setTxBufferSize(12288); // a whole photo, so sending it never blocks the loop
  CydSerial.begin(115200, SERIAL_8N1, PIN_CYD_RX, PIN_CYD_TX);
  dht.begin();
  analogSetPinAttenuation(PIN_GAS, ADC_11db);  // 0-3.1 V range
  prefs.begin("pdd");
  gasR0 = prefs.isKey("gasR0") ? prefs.getFloat("gasR0") : NAN;
  gasR0Saved = !isnan(gasR0);
  Serial.println();
  Serial.println("Poultry Disease Detector main board");
  Serial.printf("ESP32-S3, flash %lu MB, PSRAM %lu KB\r\n", (unsigned long)(ESP.getFlashChipSize() >> 20),
                (unsigned long)(ESP.getPsramSize() >> 10));
  printHelp();
  webBegin(pi, display);
}

void loop() {
  if (pi.update()) {
    printResult(pi.result);
    webAddResult(pi.result);
    strcpy(display.disease, pi.result.label);
    display.confidence = pi.result.confidence;
    sendDisplay();
  }
  if (pi.imageReady) {
    pi.imageReady = false;
    sendPhoto();
    webAddPhoto(pi.imageSeq, pi.image, pi.imageLen);
  }
  if (millis() - lastDisplayMs >= 2000) {
    readDht();
    readGas();
    sendDisplay();
  }
  if (logLinkChanges()) sendDisplay();
  handleConsole();
  webLoop();
}
