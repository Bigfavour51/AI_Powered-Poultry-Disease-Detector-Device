# Poultry Disease Detector – Raspberry Pi side

Final-year project: an embedded system that photographs chicken droppings, classifies them
(Coccidiosis, Healthy, Newcastle, Salmonella) with an ensemble of three INT8 TFLite CNNs on a
Raspberry Pi, and reports to an STM32 main board over UART.

## How this project is worked on
- Code lives and is edited **here, on the PC**. Never edit files directly on the Pi; they are
  overwritten by the next deploy.
- `./deploy.sh` rsyncs this folder to `pdd:~/poultry_pi/`. `./deploy.sh <cmd>` also runs `<cmd>`
  on the Pi inside that folder, e.g. `./deploy.sh venv/bin/python tests/test_button.py`.
- Run anything on the Pi with `ssh pdd "<command>"` (key login and passwordless sudo are set up).
- Results the Pi produces (photos, CSV logs, test images) are fetched back with `scp`/`rsync` from
  `pdd:poultry_pi/...` into the same relative paths here.

## The Pi (host alias `pdd`, user `aina`, 192.168.10.80)
- **Raspberry Pi 3 Model B+**, 1 GB RAM, 4 × Cortex-A53. Raspberry Pi OS **Trixie (Debian 13), 64-bit,
  console only** (desktop disabled), Python 3.13.
- Keep it light: no heavy packages, no builds from source, no desktop apps. Prefer `apt` packages
  (`python3-*`) and the venv at `~/poultry_pi/venv` (created with `--system-site-packages`).
- AI runtime: `ai-edge-litert` (import `ai_edge_litert.interpreter`).
- `/boot/firmware/config.txt` has `camera_auto_detect=1`, `enable_uart=1`, `dtoverlay=disable-bt`.
  Ask before changing it; a bad edit can stop the Pi booting.
- Check power with `vcgencmd get_throttled` (want `0x0`). Undervoltage silently slows inference.

## Hardware on the Pi
| Item | Connection |
|---|---|
| Camera Module 3 **Wide** (imx708_wide) | CSI port marked **CAMERA** (between audio and HDMI), not DISPLAY |
| Capture button | GPIO17 (pin 11) to GND (pin 9), internal pull-up |
| UART to STM32 | `/dev/serial0` → `ttyAMA0`. Pi TXD GPIO14 (pin 8) → STM32 PB7; Pi RXD GPIO15 (pin 10) ← STM32 PB6; GND pin 6 |
| Test adapter | FT232R USB-TTL on the PC (`/dev/ttyUSB0`), **3.3 V setting only** |

Camera: native fast mode 2304 × 1296; use `rpicam-still -n` (no preview, there is no screen).

## Code map
- `pdd_inference.py` – the service: button/CAP trigger → capture → centre crop → 224×224, /255 →
  MobileNetV2 + NASNetMobile + ShuffleNetV2 (INT8) → accuracy-weighted average → UART frame + CSV log.
  Modes: default service, `--image FILE`, `--benchmark N`, `--no-serial`, `-v`.
- `models/` – `MobileNetV2_int8.tflite`, `NASNetMobile_int8.tflite`, `ShuffleNetV2_int8.tflite`
  (from the training notebook's `poultry_disease_results2`). Not in git.
- `tests/` – `test_button.py`, `test_camera.py`, `test_uart_loopback.py` (Pi) and
  `pc_stm32_simulator.py` (runs on the PC with the FT232R).
- `stm32/pi_link.h` – STM32 (Arduino/PlatformIO) parser for the same protocol.
- `evaluate_pi_ensemble.py` – accuracy of the deployed INT8 ensemble on the validation set (laptop).
- `pdd.service` – systemd unit for running at boot.

## UART protocol (115200 8-N-1, must stay compatible with stm32/pi_link.h)
`$PDD,<TYPE>,<fields>*<CS>\r\n`, CS = XOR of bytes between `$` and `*`, two hex digits.
Pi → STM32: `BOOT`, `HB` (every 5 s), `BUSY`, `RES,<seq>,<LABEL>,<conf>,<p_cocci>,<p_healthy>,<p_ncd>,<p_salmo>,<OK|LOW>,<agree>,<ms>`, `ERR`, `PONG`.
STM32 → Pi: `CAP`, `PING`.

## Conventions
- Preprocessing must match training exactly (nearest-neighbour resize to 224×224, pixels /255,
  class order cocci, healthy, ncd, salmo). Don't change it without re-validating accuracy.
- After changing anything that touches the protocol, run the loopback test and update `stm32/pi_link.h`.
- Pi 3 is slow: if the ensemble is too slow per capture, measure with `--benchmark` before deciding
  to drop NASNetMobile.
