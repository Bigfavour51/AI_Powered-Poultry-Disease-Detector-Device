#!/usr/bin/env python3
"""
pdd_inference.py - Poultry Disease Detector (Raspberry Pi 4 side)

What it does
------------
1. Waits for a capture request, either from the external push button or from
   the STM32 over UART (the STM32 can forward a remote request from the 4G link).
2. Captures a still with the Raspberry Pi Camera Module 3 Wide (IMX708).
3. Pre-processes the image exactly as in training (resize to 224x224, scale to 0-1).
4. Runs three INT8 TensorFlow Lite models and combines them with an
   accuracy-weighted average ensemble:
       MobileNetV2 (88.61 %), NASNetMobile (88.85 %), ShuffleNetV2 (86.75 %)
5. Sends the result to the STM32 over UART in a checksummed text frame.
6. Saves the photo and a CSV log line (with per-model timings) for later analysis.

Run modes
---------
    python3 pdd_inference.py                     # normal service mode (camera + button + UART)
    python3 pdd_inference.py --image a.jpg       # classify image files, no camera/button
    python3 pdd_inference.py --benchmark 50      # time each model on the Pi (for Chapter 4)
    python3 pdd_inference.py --no-serial ...     # run without the STM32 connected

UART protocol (115200 8-N-1, ASCII, one frame per line, NMEA-style checksum)
-------------
    $PDD,<TYPE>,<fields...>*<CS>\\r\\n     CS = XOR of every byte between '$' and '*', 2 hex digits

  Pi -> STM32
    BOOT,<version>,<cam_ok>,<model_ok>
    HB,<seq>,<cam_ok>,<model_ok>,<uptime_s>                    every HEARTBEAT_S seconds
    BUSY,<seq>,<source>                                        capture started (BTN or REM)
    RES,<seq>,<LABEL>,<conf>,<p_cocci>,<p_healthy>,<p_ncd>,<p_salmo>,<flag>,<agree>,<inf_ms>
         conf and p_* are whole percentages; flag is OK or LOW (conf below threshold);
         agree is how many of the 3 models picked the same class (1-3)
    ERR,<seq>,<code>                                           code = CAM, MODEL or IMG
    PONG

  STM32 -> Pi
    CAP     request a capture (e.g. remote command received over 4G)
    PING    link check, answered with PONG
"""

import argparse
import csv
import logging
import queue
import signal
import sys
import threading
import time
from datetime import datetime
from pathlib import Path

import numpy as np
from PIL import Image

VERSION = "1.0"

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
BASE_DIR = Path(__file__).resolve().parent
MODEL_DIR = BASE_DIR / "models"
CAPTURE_DIR = BASE_DIR / "captures"
LOG_CSV = BASE_DIR / "logs" / "detections.csv"

# Order produced by Keras flow_from_directory (alphabetical folder names)
CLASS_KEYS = ["cocci", "healthy", "ncd", "salmo"]
CLASS_LABELS = {
    "cocci": "COCCIDIOSIS",
    "healthy": "HEALTHY",
    "ncd": "NEWCASTLE",
    "salmo": "SALMONELLA",
}

# (file name, test accuracy % from the training notebook). Accuracy is the ensemble weight.
MODELS = [
    ("MobileNetV2_int8.tflite", 88.61),
    ("NASNetMobile_int8.tflite", 88.85),
    ("ShuffleNetV2_int8.tflite", 86.75),
]

IMG_SIZE = (224, 224)
CAMERA_SIZE = (2304, 1296)   # native 2x2-binned mode of the IMX708 sensor (fast, low noise)
CROP_FRACTION = 0.8          # centre square crop, as a fraction of frame height
# Keras flow_from_directory resizes with nearest-neighbour by default; match it.
RESAMPLE = Image.NEAREST

BUTTON_GPIO = 17             # BCM 17 = physical pin 11; button to GND (pin 9)
SERIAL_PORT = "/dev/serial0" # GPIO14 TXD (pin 8) / GPIO15 RXD (pin 10)
BAUD = 115200
HEARTBEAT_S = 5.0
LOW_CONF_THRESHOLD = 60      # % - below this the result is flagged LOW
NUM_THREADS = 4              # Pi 4 has four Cortex-A72 cores

log = logging.getLogger("pdd")


# ---------------------------------------------------------------------------
# UART framing
# ---------------------------------------------------------------------------
def checksum(body: str) -> str:
    cs = 0
    for ch in body.encode("ascii"):
        cs ^= ch
    return f"{cs:02X}"


def make_frame(*fields) -> bytes:
    body = "PDD," + ",".join(str(f) for f in fields)
    return f"${body}*{checksum(body)}\r\n".encode("ascii")


def parse_frame(line: str):
    """Return the list of fields after 'PDD' for a valid frame, otherwise None."""
    line = line.strip()
    if not line.startswith("$") or "*" not in line:
        return None
    body, _, cs = line[1:].rpartition("*")
    if len(cs) != 2 or checksum(body) != cs.upper():
        return None
    parts = body.split(",")
    if not parts or parts[0] != "PDD":
        return None
    return parts[1:]


class SerialLink:
    """Thread-safe UART link to the STM32. Incoming commands go onto a queue."""

    def __init__(self, port, baud, events: queue.Queue):
        import serial  # pyserial; imported here so --no-serial runs without it
        self.ser = serial.Serial(port, baud, bytesize=8, parity="N", stopbits=1, timeout=0.1)
        self.events = events
        self.lock = threading.Lock()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()

    def send(self, *fields):
        frame = make_frame(*fields)
        with self.lock:
            self.ser.write(frame)
        log.debug("TX %s", frame.decode().strip())

    def _reader(self):
        buf = b""
        while not self._stop.is_set():
            try:
                chunk = self.ser.read(64)
            except Exception as exc:  # cable pulled, etc.
                log.warning("UART read error: %s", exc)
                time.sleep(1)
                continue
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                fields = parse_frame(raw.decode("ascii", "ignore"))
                if fields is None:
                    log.debug("RX ignored: %r", raw)
                    continue
                cmd = fields[0]
                if cmd == "CAP":
                    self.events.put("REM")
                elif cmd == "PING":
                    self.send("PONG")
            if len(buf) > 512:  # garbage with no newline; drop it
                buf = b""

    def close(self):
        self._stop.set()
        self.ser.close()


class NullLink:
    """Stand-in used with --no-serial: prints frames instead of sending them."""

    def send(self, *fields):
        log.info("TX (no serial) %s", make_frame(*fields).decode().strip())

    def close(self):
        pass


# ---------------------------------------------------------------------------
# Models
# ---------------------------------------------------------------------------
def get_interpreter_class():
    """Use whichever TFLite runtime is installed (LiteRT is the current one)."""
    try:
        from ai_edge_litert.interpreter import Interpreter
        return Interpreter
    except ImportError:
        pass
    try:
        from tflite_runtime.interpreter import Interpreter
        return Interpreter
    except ImportError:
        pass
    import tensorflow as tf
    return tf.lite.Interpreter


class TFLiteModel:
    def __init__(self, path: Path, interpreter_cls, threads=NUM_THREADS):
        self.name = path.stem.replace("_int8", "")
        self.interp = interpreter_cls(model_path=str(path), num_threads=threads)
        self.interp.allocate_tensors()
        self.inp = self.interp.get_input_details()[0]
        self.out = self.interp.get_output_details()[0]

    def predict(self, img01: np.ndarray) -> np.ndarray:
        """img01: float32 array (224, 224, 3) scaled to 0-1. Returns 4 class probabilities."""
        x = img01
        dtype = self.inp["dtype"]
        if np.issubdtype(dtype, np.integer):
            scale, zero = self.inp["quantization"]
            if scale > 0:
                x = np.round(x / scale + zero)
            info = np.iinfo(dtype)
            x = np.clip(x, info.min, info.max)
        x = x.astype(dtype)[np.newaxis, ...]
        self.interp.set_tensor(self.inp["index"], x)
        self.interp.invoke()
        y = self.interp.get_tensor(self.out["index"])[0]
        if np.issubdtype(self.out["dtype"], np.integer):
            scale, zero = self.out["quantization"]
            y = (y.astype(np.float32) - zero) * (scale if scale > 0 else 1.0)
        y = np.clip(y.astype(np.float32), 0.0, None)
        total = y.sum()
        return y / total if total > 0 else np.full(len(y), 1.0 / len(y), np.float32)


class Ensemble:
    def __init__(self, model_dir: Path, specs=MODELS, threads=NUM_THREADS):
        interpreter_cls = get_interpreter_class()
        self.models, acc = [], []
        for fname, accuracy in specs:
            path = model_dir / fname
            if not path.exists():
                raise FileNotFoundError(f"Model not found: {path}")
            self.models.append(TFLiteModel(path, interpreter_cls, threads))
            acc.append(accuracy)
        acc = np.array(acc, dtype=np.float32)
        self.weights = acc / acc.sum()

    def warm_up(self):
        blank = np.zeros((*IMG_SIZE, 3), np.float32)
        for m in self.models:
            m.predict(blank)

    def predict(self, img01: np.ndarray) -> dict:
        per_model, times = [], []
        for m in self.models:
            t0 = time.perf_counter()
            per_model.append(m.predict(img01))
            times.append((time.perf_counter() - t0) * 1000)
        probs = np.tensordot(self.weights, np.stack(per_model), axes=1)
        top = int(np.argmax(probs))
        agree = sum(int(np.argmax(p)) == top for p in per_model)
        return {
            "class_key": CLASS_KEYS[top],
            "label": CLASS_LABELS[CLASS_KEYS[top]],
            "confidence": float(probs[top]) * 100,
            "probs": probs,
            "per_model": dict(zip((m.name for m in self.models), per_model)),
            "per_model_ms": dict(zip((m.name for m in self.models), times)),
            "agree": agree,
            "total_ms": sum(times),
        }


# ---------------------------------------------------------------------------
# Image handling
# ---------------------------------------------------------------------------
def preprocess(img: Image.Image, crop_fraction=CROP_FRACTION) -> np.ndarray:
    """Centre-square crop (wide lens sees a lot of floor), resize, scale to 0-1.
    crop_fraction=None resizes the whole image, exactly as the training pipeline did."""
    img = img.convert("RGB")
    if crop_fraction:
        w, h = img.size
        side = int(min(w, h) * crop_fraction)
        left, top = (w - side) // 2, (h - side) // 2
        img = img.crop((left, top, left + side, top + side))
    img = img.resize(IMG_SIZE, RESAMPLE)
    return np.asarray(img, dtype=np.float32) / 255.0


class Camera:
    def __init__(self, size=CAMERA_SIZE):
        from picamera2 import Picamera2
        self.cam = Picamera2()
        cfg = self.cam.create_still_configuration(main={"size": size})
        self.cam.configure(cfg)
        self.cam.start()
        time.sleep(1.0)  # let auto-exposure and white balance settle
        try:
            from libcamera import controls
            self.cam.set_controls({"AfMode": controls.AfModeEnum.Auto})
            self.has_af = True
        except Exception:
            self.has_af = False

    def capture(self) -> Image.Image:
        if self.has_af:
            try:
                self.cam.autofocus_cycle()
            except Exception as exc:
                log.warning("Autofocus failed, capturing anyway: %s", exc)
        return self.cam.capture_image("main")  # PIL image, RGB

    def close(self):
        self.cam.stop()
        self.cam.close()


# ---------------------------------------------------------------------------
# Logging results
# ---------------------------------------------------------------------------
def append_csv(path: Path, row: dict):
    path.parent.mkdir(parents=True, exist_ok=True)
    new = not path.exists()
    with open(path, "a", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(row.keys()))
        if new:
            writer.writeheader()
        writer.writerow(row)


def result_row(seq, source, image_path, res):
    row = {
        "timestamp": datetime.now().isoformat(timespec="seconds"),
        "seq": seq,
        "source": source,
        "image": image_path,
        "label": res["label"],
        "confidence": round(res["confidence"], 2),
        "agree": res["agree"],
        "total_ms": round(res["total_ms"], 1),
    }
    for k, p in zip(CLASS_KEYS, res["probs"]):
        row[f"p_{k}"] = round(float(p) * 100, 2)
    for name, ms in res["per_model_ms"].items():
        row[f"ms_{name}"] = round(ms, 1)
    for name, p in res["per_model"].items():
        row[f"pred_{name}"] = CLASS_KEYS[int(np.argmax(p))]
    return row


def send_result(link, seq, res, threshold=LOW_CONF_THRESHOLD):
    conf = int(round(res["confidence"]))
    pcts = [int(round(float(p) * 100)) for p in res["probs"]]
    flag = "OK" if conf >= threshold else "LOW"
    link.send("RES", seq, res["label"], conf, *pcts, flag, res["agree"], int(round(res["total_ms"])))


# ---------------------------------------------------------------------------
# Run modes
# ---------------------------------------------------------------------------
def run_service(args):
    events: queue.Queue = queue.Queue()
    link = NullLink() if args.no_serial else SerialLink(args.port, BAUD, events)

    model_ok, ensemble = False, None
    try:
        ensemble = Ensemble(Path(args.models))
        ensemble.warm_up()
        model_ok = True
        log.info("Models loaded: %s", ", ".join(m.name for m in ensemble.models))
    except Exception as exc:
        log.error("Model load failed: %s", exc)

    cam_ok, camera = False, None
    try:
        camera = Camera()
        cam_ok = True
        log.info("Camera ready (autofocus: %s)", camera.has_af)
    except Exception as exc:
        log.error("Camera init failed: %s", exc)

    button = None
    try:
        from gpiozero import Button
        button = Button(args.button_gpio, pull_up=True, bounce_time=0.05)
        button.when_pressed = lambda: events.put("BTN")
        log.info("Button on GPIO%d", args.button_gpio)
    except Exception as exc:
        log.warning("Button unavailable (%s); remote CAP still works", exc)

    stop = threading.Event()
    signal.signal(signal.SIGTERM, lambda *_: stop.set())
    signal.signal(signal.SIGINT, lambda *_: stop.set())

    start = time.monotonic()
    seq = 0
    link.send("BOOT", VERSION, int(cam_ok), int(model_ok))
    next_hb = time.monotonic()

    while not stop.is_set():
        now = time.monotonic()
        if now >= next_hb:
            link.send("HB", seq, int(cam_ok), int(model_ok), int(now - start))
            next_hb = now + HEARTBEAT_S
        try:
            source = events.get(timeout=0.2)
        except queue.Empty:
            continue

        # Ignore presses queued up while the previous capture was running
        while not events.empty():
            events.get_nowait()

        seq += 1
        link.send("BUSY", seq, source)
        if not cam_ok:
            link.send("ERR", seq, "CAM")
            continue
        if not model_ok:
            link.send("ERR", seq, "MODEL")
            continue
        try:
            img = camera.capture()
        except Exception as exc:
            log.error("Capture failed: %s", exc)
            link.send("ERR", seq, "CAM")
            continue

        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        CAPTURE_DIR.mkdir(parents=True, exist_ok=True)
        img_path = CAPTURE_DIR / f"{stamp}_{seq:05d}.jpg"
        img.save(img_path, quality=90)

        try:
            res = ensemble.predict(preprocess(img, args.crop))
        except Exception as exc:
            log.error("Inference failed: %s", exc)
            link.send("ERR", seq, "IMG")
            continue

        send_result(link, seq, res, args.threshold)
        append_csv(Path(args.log), result_row(seq, source, img_path.name, res))
        log.info("#%d %s %.1f%% (agree %d/3, %.0f ms)",
                 seq, res["label"], res["confidence"], res["agree"], res["total_ms"])

    log.info("Shutting down")
    if camera:
        camera.close()
    link.close()


def run_images(args):
    events: queue.Queue = queue.Queue()
    link = NullLink() if args.no_serial else SerialLink(args.port, BAUD, events)
    ensemble = Ensemble(Path(args.models))
    ensemble.warm_up()
    for seq, path in enumerate(args.image, start=1):
        res = ensemble.predict(preprocess(Image.open(path), args.crop))
        send_result(link, seq, res, args.threshold)
        append_csv(Path(args.log), result_row(seq, "FILE", Path(path).name, res))
        detail = "  ".join(f"{k}={p * 100:.1f}%" for k, p in zip(CLASS_KEYS, res["probs"]))
        print(f"{path}: {res['label']} {res['confidence']:.1f}% (agree {res['agree']}/3)  {detail}")
    link.close()


def run_benchmark(args):
    ensemble = Ensemble(Path(args.models))
    ensemble.warm_up()
    rng = np.random.default_rng(0)
    timings = {m.name: [] for m in ensemble.models}
    for _ in range(args.benchmark):
        img = rng.random((*IMG_SIZE, 3), dtype=np.float32)
        for name, ms in ensemble.predict(img)["per_model_ms"].items():
            timings[name].append(ms)
    print(f"Inference time over {args.benchmark} runs, {NUM_THREADS} threads")
    print(f"{'Model':<16}{'mean ms':>10}{'std ms':>10}{'min ms':>10}{'max ms':>10}")
    total = np.zeros(args.benchmark)
    for name, t in timings.items():
        t = np.array(t)
        total += t
        print(f"{name:<16}{t.mean():>10.1f}{t.std():>10.1f}{t.min():>10.1f}{t.max():>10.1f}")
    print(f"{'Ensemble':<16}{total.mean():>10.1f}{total.std():>10.1f}{total.min():>10.1f}{total.max():>10.1f}")


def main():
    ap = argparse.ArgumentParser(description="Poultry Disease Detector - Raspberry Pi inference service")
    ap.add_argument("--image", nargs="+", help="classify these image files instead of using the camera")
    ap.add_argument("--benchmark", type=int, metavar="N", help="time each model over N runs and exit")
    ap.add_argument("--no-serial", action="store_true", help="do not open the UART (print frames instead)")
    ap.add_argument("--port", default=SERIAL_PORT)
    ap.add_argument("--models", default=str(MODEL_DIR))
    ap.add_argument("--log", default=str(LOG_CSV))
    ap.add_argument("--button-gpio", type=int, default=BUTTON_GPIO)
    ap.add_argument("--threshold", type=int, default=LOW_CONF_THRESHOLD)
    ap.add_argument("--crop", type=float, default=CROP_FRACTION,
                    help="centre crop fraction; 0 = resize the whole frame")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format="%(asctime)s %(levelname)s %(message)s")
    if args.benchmark:
        run_benchmark(args)
    elif args.image:
        run_images(args)
    else:
        run_service(args)


if __name__ == "__main__":
    sys.exit(main())
