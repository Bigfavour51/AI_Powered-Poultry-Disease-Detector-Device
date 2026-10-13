#!/usr/bin/env python3
"""
evaluate_pi_ensemble.py - run on the training laptop (Anaconda, TensorFlow 2.21)

Measures the accuracy of exactly what runs on the Raspberry Pi: the three INT8
TFLite models and their accuracy-weighted average ensemble, using the same
pre-processing as pdd_inference.py. Use these numbers in Chapter 4.

    python evaluate_pi_ensemble.py --data "C:/.../extracted_data/val" --models "C:/Users/user/Desktop/poultry_disease_results2"

Outputs (in --out):
    pi_ensemble_results.csv        accuracy / F1 per model and for the ensemble
    pi_ensemble_report.txt         per-class precision, recall, F1
    cm_pi_ensemble.png             normalised confusion matrix of the ensemble
    pi_ensemble_predictions.csv    one row per image (for error analysis)

Note: the training notebook had no separate test folder, so 'val' was used both to
pick the ensemble weights and to report accuracy. Treat the ensemble figure as
slightly optimistic.
"""
import argparse
import csv
from pathlib import Path

import numpy as np
from PIL import Image

from pdd_inference import CLASS_KEYS, MODELS, Ensemble, preprocess

EXTS = {".jpg", ".jpeg", ".png", ".bmp"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True, help="folder with cocci/ healthy/ ncd/ salmo/ sub-folders")
    ap.add_argument("--models", required=True, help="folder containing the *_int8.tflite files")
    ap.add_argument("--out", default="pi_eval_results")
    args = ap.parse_args()

    from sklearn.metrics import accuracy_score, classification_report, confusion_matrix, f1_score

    files, labels = [], []
    for idx, key in enumerate(CLASS_KEYS):
        folder = Path(args.data) / key
        imgs = sorted(p for p in folder.iterdir() if p.suffix.lower() in EXTS)
        files += imgs
        labels += [idx] * len(imgs)
        print(f"{key:<8} {len(imgs)} images")
    labels = np.array(labels)

    ens = Ensemble(Path(args.models), MODELS, threads=4)
    names = [m.name for m in ens.models]
    per_model_pred = {n: [] for n in names}
    ens_pred, rows = [], []
    times = []

    for i, path in enumerate(files):
        res = ens.predict(preprocess(Image.open(path), crop_fraction=None))  # whole image, as in training
        top = CLASS_KEYS.index(res["class_key"])
        ens_pred.append(top)
        times.append(res["total_ms"])
        for n, p in res["per_model"].items():
            per_model_pred[n].append(int(np.argmax(p)))
        rows.append({"file": path.name, "true": CLASS_KEYS[labels[i]], "pred": res["class_key"],
                     "confidence": round(res["confidence"], 2), "agree": res["agree"]})
        if (i + 1) % 100 == 0:
            print(f"  {i + 1}/{len(files)}")

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    ens_pred = np.array(ens_pred)

    with open(out / "pi_ensemble_results.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Model", "Accuracy (%)", "Weighted F1", "Macro F1"])
        for n in names:
            p = np.array(per_model_pred[n])
            w.writerow([f"{n} (INT8)", round(accuracy_score(labels, p) * 100, 2),
                        round(f1_score(labels, p, average="weighted"), 4),
                        round(f1_score(labels, p, average="macro"), 4)])
        w.writerow(["3-model weighted ensemble (INT8)", round(accuracy_score(labels, ens_pred) * 100, 2),
                    round(f1_score(labels, ens_pred, average="weighted"), 4),
                    round(f1_score(labels, ens_pred, average="macro"), 4)])

    report = classification_report(labels, ens_pred, target_names=CLASS_KEYS, digits=4)
    (out / "pi_ensemble_report.txt").write_text(report)

    with open(out / "pi_ensemble_predictions.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    cm = confusion_matrix(labels, ens_pred)
    cmn = cm / cm.sum(axis=1, keepdims=True)
    fig, ax = plt.subplots(figsize=(6, 5))
    im = ax.imshow(cmn, cmap="Blues", vmin=0, vmax=1)
    ax.set_xticks(range(4), CLASS_KEYS)
    ax.set_yticks(range(4), CLASS_KEYS)
    for r in range(4):
        for c in range(4):
            ax.text(c, r, f"{cmn[r, c]:.2f}\n({cm[r, c]})", ha="center", va="center",
                    color="white" if cmn[r, c] > 0.5 else "black", fontsize=9)
    ax.set_xlabel("Predicted label")
    ax.set_ylabel("True label")
    ax.set_title("Confusion Matrix - 3-Model INT8 Ensemble")
    fig.colorbar(im, ax=ax)
    fig.tight_layout()
    fig.savefig(out / "cm_pi_ensemble.png", dpi=200)

    print("\n" + (out / "pi_ensemble_results.csv").read_text())
    print(report)
    print(f"Mean ensemble time on this PC: {np.mean(times):.1f} ms/image (run --benchmark on the Pi for Pi timings)")
    print(f"Saved to {out.resolve()}")


if __name__ == "__main__":
    main()
