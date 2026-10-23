#!/usr/bin/env python3
"""Camera test: capture exactly as the service does and save what the model sees.

Saves:
  tests/out/cam_full.jpg    the full 2304 x 1296 capture
  tests/out/cam_model.png   the 224 x 224 centre crop that is fed to the models

Run:  ./deploy.sh venv/bin/python tests/test_camera.py
then copy the images back with:  scp 'pdd:poultry_pi/tests/out/cam_*' tests/out/
"""
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pdd_inference import CROP_FRACTION, Camera, preprocess  # noqa: E402

out = Path(__file__).resolve().parent / "out"
out.mkdir(exist_ok=True)

t0 = time.perf_counter()
cam = Camera()
print(f"Camera ready in {time.perf_counter() - t0:.1f} s (autofocus: {cam.has_af})")

t0 = time.perf_counter()
img = cam.capture()
print(f"Captured {img.size[0]} x {img.size[1]} in {time.perf_counter() - t0:.2f} s (includes autofocus)")
cam.close()

img.save(out / "cam_full.jpg", quality=90)
x = preprocess(img, CROP_FRACTION)
Image.fromarray(np.uint8(x * 255)).save(out / "cam_model.png")

print(f"Mean brightness of model input: {x.mean():.2f} (0 = black, 1 = white)")
if x.mean() < 0.08:
    print("WARNING: image is very dark - check the lens cap, lighting or exposure")
print(f"Saved {out / 'cam_full.jpg'} and {out / 'cam_model.png'}")
