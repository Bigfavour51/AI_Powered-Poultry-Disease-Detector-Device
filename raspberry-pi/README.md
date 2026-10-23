# Poultry Disease Detector: Raspberry Pi software

Button-triggered droppings capture, 3-model AI ensemble, and UART link to the STM32 main board.
Developed on the PC in VS Code (with Claude Code), deployed to the Pi over SSH.

## Folder contents

| Path | Runs on | Purpose |
|---|---|---|
| `pdd_inference.py` | Pi | Main service: button/remote trigger, camera capture, INT8 TFLite ensemble, UART frames, image + CSV logging |
| `tests/test_button.py` | Pi | Counts button presses on GPIO17 |
| `tests/test_camera.py` | Pi | Captures like the service and saves the full photo and the 224 × 224 model input |
| `tests/test_uart_loopback.py` | Pi | Sends frames out of TXD and checks they come back on RXD (jumper pin 8 to pin 10) |
| `tests/pc_stm32_simulator.py` | PC | Plays the STM32's role through the FT232R adapter: sends CAP/PING, decodes the Pi's frames |
| `deploy.sh` | PC | Syncs this folder to the Pi; optionally runs a command there |
| `.vscode/tasks.json` | PC | One-click tasks for all of the above (Ctrl+Shift+P → Run Task) |
| `CLAUDE.md` | PC | Project briefing that Claude Code reads automatically |
| `stm32/pi_link.h` | STM32 | Receiver for the Pi's frames (Arduino/PlatformIO) |
| `evaluate_pi_ensemble.py` | Laptop | Accuracy of the deployed INT8 ensemble on the validation set |
| `pdd.service` | Pi | systemd unit to start the service at boot |

## One-time setup

**PC** (already done: SSH key and `Host pdd` in `~/.ssh/config`):
```bash
chmod +x deploy.sh
pip install pyserial        # only needed for the STM32 simulator
```

**Pi** (run from the PC):
```bash
ssh pdd "sudo apt install -y rsync python3-picamera2 python3-gpiozero python3-serial python3-pil python3-numpy python3-venv"
ssh pdd "mkdir -p ~/poultry_pi && cd ~/poultry_pi && python3 -m venv --system-site-packages venv && venv/bin/pip install ai-edge-litert"
./deploy.sh venv/bin/python -c "from ai_edge_litert.interpreter import Interpreter; import picamera2, gpiozero, serial; print('ALL OK')"
```

**Models:** copy `MobileNetV2_int8.tflite`, `NASNetMobile_int8.tflite` and `ShuffleNetV2_int8.tflite`
from `poultry_disease_results2` (or its results zip) into `models/` here. The next deploy sends them to the Pi.

## Bring-up order

| Step | VS Code task | Pass when |
|---|---|---|
| 1 | Pi: camera test (fetch images) | `tests/out/cam_model.png` shows the droppings framed and in focus |
| 2 | Pi: button test | Each press is counted once |
| 3 | Pi: UART loopback test (jumper pin 8 to 10) | 50/50 frames returned intact |
| 4 | Pi: benchmark models | Note the per-model times for Chapter 4, Table 4.7 |
| 5 | Pi: run service (verbose, no STM32) | Button press prints a `RES` frame |
| 6 | Pi: run service (with UART) + PC: STM32 simulator | Simulator shows heartbeats, and `c` triggers a capture and a decoded result |
| 7 | Install `pdd.service` | Service starts on boot |

FT232R wiring for step 6 (**adapter jumper on 3.3 V**): Pi pin 8 → adapter RXD, Pi pin 10 ← adapter TXD, Pi pin 6 → GND.

Run at boot (step 7):
```bash
./deploy.sh "sudo cp pdd.service /etc/systemd/system/ && sudo systemctl enable --now pdd && systemctl status pdd --no-pager"
ssh pdd "journalctl -u pdd -f"      # live log
```
While the service is running it owns the camera, the button and the UART, so stop it before running
the tests: `ssh pdd "sudo systemctl stop pdd"`.

## UART protocol (115200 8-N-1)

Frame: `$PDD,<TYPE>,<fields>*<CS>\r\n`, CS = XOR of every byte between `$` and `*` (two hex digits).

| Direction | Frame | Meaning |
|---|---|---|
| Pi → STM32 | `BOOT,<ver>,<cam_ok>,<model_ok>` | Service started |
| Pi → STM32 | `HB,<seq>,<cam_ok>,<model_ok>,<uptime_s>` | Heartbeat every 5 s |
| Pi → STM32 | `BUSY,<seq>,<BTN\|REM>` | Capture started |
| Pi → STM32 | `RES,<seq>,<LABEL>,<conf>,<p_cocci>,<p_healthy>,<p_ncd>,<p_salmo>,<OK\|LOW>,<agree>,<ms>` | Result |
| Pi → STM32 | `ERR,<seq>,<CAM\|MODEL\|IMG>` | Failure |
| Pi → STM32 | `PONG` | Reply to PING |
| STM32 → Pi | `CAP` / `PING` | Request capture / link check |

Example: `$PDD,RES,12,COCCIDIOSIS,87,87,6,4,3,OK,3,412*44`

## Outputs on the Pi
- `captures/YYYYMMDD_HHMMSS_<seq>.jpg`: every photo.
- `logs/detections.csv`: label, confidence, all class probabilities, each model's prediction and time.
  Fetch both with the "Pi: fetch captures and logs" task.
