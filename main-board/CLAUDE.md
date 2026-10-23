# Poultry Disease Detector – main board (ESP32-S3)

Final-year project. The main board talks to a Raspberry Pi that photographs chicken
droppings and classifies them (Coccidiosis, Healthy, Newcastle, Salmonella). The main board
requests captures, receives results, reads the shed sensors (DHT temperature/humidity, MQ135
gas) and shows everything on a CYD display. The 4G module is out of scope.
This repo holds the whole system: `main-board/` (this firmware), `../raspberry-pi/` (camera + AI,
its own CLAUDE.md) and `../cyd-display/` (ESP32-2432S028R, LVGL 9 + EEZ Studio UI, `pio` env
`esp32dev`). `../docs/` is the GitHub Pages showcase (deployed by `.github/workflows/pages.yml`).

Built one feature at a time. Done: Pi link (incl. photo), CYD display link, DHT11, MQ135 (calibrated).
Current step: **phone dashboard** over the S3's WiFi access point.
The board moved from a Blue Pill to an ESP32-S3 (the STM32 version is in the git history before
this repo was combined).

## Board and tools
- **ESP32-S3-DevKitC-1 N16R8** (ESP32-S3 QFN56 rev v0.2, 16 MB quad flash, 8 MB octal PSRAM),
  PlatformIO env `esp32s3`, Arduino framework (arduino-esp32 2.0.17).
  `board_build.arduino.memory_type = qio_opi` is required for the octal PSRAM.
- Plugged in through the board's **"UART" USB port** (CH343 bridge, `1a86:55d3`) →
  **`/dev/ttyACM0`**. That is UART0 (GPIO43 TX / GPIO44 RX) = `Serial`.
- PlatformIO CLI: `~/.platformio/penv/bin/pio` (not on PATH).

## Build / flash / monitor
- Build: `pio run` (always build after editing).
- Flash: `pio run -t upload`. Auto-reset into the bootloader, no buttons needed.
  Still ask the user before uploading (it overwrites what is on the board).
- Debug console: `pio device monitor` (115200). Close the monitor before uploading.

## Pin map (`include/pins.h`)
| UART | Pins | Used for |
|---|---|---|
| UART0 | GPIO43 TX / GPIO44 RX | CH343 USB bridge: flashing + debug log (`Serial`) |
| UART1 | GPIO17 TX / GPIO18 RX | Raspberry Pi: GPIO18 ← Pi GPIO14 TXD (pin 8), GPIO17 → Pi GPIO15 RXD (pin 10), GND to Pi pin 6 |
| UART2 | GPIO41 TX / GPIO42 RX | CYD display: GPIO41 → CYD GPIO35 (input-only, CYD RX), GPIO42 ← CYD GPIO27 (CYD TX), GND shared |
Relay module IN: GPIO3, held off (`RELAY_OFF` = HIGH) until it gets a use.
DHT11 data: GPIO13 (3.3 V supply, Adafruit DHT library, read every 2 s).
MQ135 AO: GPIO7 (ADC1; module on 3.3 V, no divider). NH3 estimate from the datasheet curve;
R0 calibrated with console `g` in clean air (saved in NVS), else auto after 60 s warm-up.
LED: onboard WS2812 RGB on GPIO48 (`LED_BUILTIN`, driven with `digitalWrite`, HIGH = on).
Avoid GPIO0/3/45/46 (strapping), 19/20 (native USB), 26–32 (flash), 33–37 (octal PSRAM).
The CYD is powered from the S3's **5V** pin: its onboard 3.3 V regulator needs 5 V in, so 3.3 V
does not start it.

## Raspberry Pi link
- `lib/PiLink/pi_link.h` implements the protocol: `$PDD,<TYPE>,<fields>*<CS>\r\n`,
  115200 8-N-1, CS = XOR of bytes between `$` and `*`.
  Pi → board: `BOOT`, `HB` (every 5 s), `BUSY`, `RES`, `ERR`, `PONG`. Board → Pi: `CAP`, `PING`.
- On ESP32 the pins are passed at start-up: `pi.begin(PIN_PI_RX, PIN_PI_TX)`. The STM32 build
  (`begin()` with no pins) is kept so the Blue Pill test sketch still compiles.
- It is a **copy of `../raspberry-pi/stm32/pi_link.h`**; the two must stay identical.
  Protocol changes start on the Pi side (`pdd_inference.py`); update both headers together.
- Parser tests without the Pi: `../raspberry-pi/stm32/test_pi_link` (Blue Pill) +
  `../raspberry-pi/tests/pc_pi_simulator.py`.

## CYD display link
- `lib/CydLink/cyd_link.h` (format documented at its top): a TEL frame from the S3 every 2 s and
  on every change (sensors, result, Pi status dots, uptime), plus IMG/IMD frames carrying the
  result photo. `-` = no value (shown as `--`). Same checksum as the Pi link.
- The Pi sends the photo (112x84 baseline JPEG) after each RES as IMG/IMD; the S3 forwards it;
  the CYD decodes it with LVGL's bundled TJpgDec into the camera panel.
- Identical copy in `../cyd-display/lib/CydLink/cyd_link.h`; change both together.
  The CYD converts it to its UI struct in `src/uart_driver.cpp`; `USE_SIMULATOR` in its
  `main.cpp` switches back to random test data.

## Phone dashboard (WiFi access point)
- `src/web.cpp` + `include/web.h`: the S3 runs an **open** AP **"PoultryDetector"** (no password:
  the WPA2 handshake never completed on this board; set at the top of web.cpp), a captive-portal DNS (every name -> 192.168.4.1, so
  phones open the page by themselves) and a WebServer on port 80, all driven from `webLoop()`.
- `src/web_page.h`: the whole page (HTML/CSS/JS inline, no CDNs - the phone has no internet there).
  Dark theme matching the CYD. Polls `/api/state` (2 s) and `/api/history` (10 s); photos from
  `/photo?seq=N`; `POST /api/capture` = console `c`.
- The last 12 detections and their photos are kept in PSRAM; sensor history is 90 samples x 10 s.
- To preview the page without the board: extract the raw string to an .html file and serve mock
  `/api/state`, `/api/history`, `/photo` (a Python http.server is enough), then open it in a browser.
