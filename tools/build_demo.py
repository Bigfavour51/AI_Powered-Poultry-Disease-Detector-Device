#!/usr/bin/env python3
"""
build_demo.py - builds docs/demo/index.html, the GitHub Pages demo of the phone dashboard.

It takes the exact page the ESP32-S3 serves (main-board/src/web_page.h) and adds a simulation of
the main board in front of it: drifting sensor readings, Pi status, and capture cycles that end in a
result with a generated sample photo. Nothing in the dashboard code itself is changed, so the demo
cannot drift away from the firmware. Run it after editing web_page.h:

    python3 tools/build_demo.py
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "main-board" / "src" / "web_page.h"
OUT = ROOT / "docs" / "demo" / "index.html"

BANNER = """
<div style="margin:0 0 14px;padding:10px 14px;border-radius:12px;border:1px dashed #2f4b60;
  background:rgba(143,224,74,.06);color:#b6c2cb;font-size:13px;line-height:1.45">
  <b style="color:#f0efec">Demo · simulated data.</b> This is the dashboard the ESP32-S3 serves on its own
  WiFi network (192.168.4.1). Here the readings, detections and photos are simulated in your browser —
  try <b style="color:#f0efec">Capture &amp; Analyse</b>.
  <a href="../" style="color:#8fe04a">Back to the project</a>
</div>
"""

SIM = r"""
<script>
/* ---- Simulated main board (GitHub Pages demo only) ---------------------------------------- */
(function () {
  const SHOT = location.search.includes("shot");        // screenshot mode: no animation, fixed data
  if (SHOT) {
    window.requestAnimationFrame = f => f();
    document.write("<style>*{transition:none!important;animation:none!important}</style>");
  }
  let seed = 7;
  const rnd = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
  const t0 = Date.now(), UP0 = 2 * 3600 + 754;
  const up = () => UP0 + Math.floor((Date.now() - t0) / 1000);
  const KEYS = ["COCCIDIOSIS", "HEALTHY", "NEWCASTLE", "SALMONELLA"];

  // Sensors: slow daily-like drift + noise; history = 90 samples, one per 10 s
  const sensor = s => ({
    t: +(26.2 + 1.3 * Math.sin(s / 520) + (rnd() - .5) * .3).toFixed(1),
    h: Math.round(57 + 5 * Math.sin(s / 700 + 1) + (rnd() - .5) * .8),
    n: +(3.4 + 2.2 * Math.max(0, Math.sin(s / 260)) + (rnd() - .5) * .4).toFixed(1)
  });
  const hist = { every: 10, t: [], h: [], n: [] };
  for (let i = 89; i >= 0; i--) { const v = sensor(UP0 - i * 10); hist.t.push(v.t); hist.h.push(v.h); hist.n.push(v.n); }
  let now = sensor(UP0), lastHist = UP0;

  // Sample photo: litter texture with droppings, tinted by class (illustration, not a real capture)
  const photos = {};
  function photo(seq, label) {
    const c = document.createElement("canvas"); c.width = 224; c.height = 168;
    const g = c.getContext("2d");
    const grd = g.createLinearGradient(0, 0, 224, 168);
    grd.addColorStop(0, "#c9b48a"); grd.addColorStop(1, "#a58f66");
    g.fillStyle = grd; g.fillRect(0, 0, 224, 168);
    for (let i = 0; i < 900; i++) {           // wood-shaving litter
      g.strokeStyle = `rgba(${90 + rnd() * 90},${70 + rnd() * 60},${40 + rnd() * 30},${.25 + rnd() * .35})`;
      g.lineWidth = .6 + rnd() * 1.4; g.beginPath();
      const x = rnd() * 224, y = rnd() * 168, a = rnd() * 6.3, l = 3 + rnd() * 9;
      g.moveTo(x, y); g.lineTo(x + Math.cos(a) * l, y + Math.sin(a) * l); g.stroke();
    }
    const tint = { HEALTHY: ["#4a3a26", "#6b5a3c"], COCCIDIOSIS: ["#5a2a1c", "#8e3b2a"],
                   NEWCASTLE: ["#3e4a26", "#6f7a3a"], SALMONELLA: ["#4a3e2c", "#d9d2bd"] }[label];
    for (let k = 0; k < 3; k++) {             // droppings
      const cx = 60 + rnd() * 110, cy = 45 + rnd() * 80, r = 12 + rnd() * 14;
      g.fillStyle = "rgba(0,0,0,.18)"; g.beginPath(); g.ellipse(cx + 3, cy + 4, r * 1.3, r, rnd(), 0, 6.3); g.fill();
      g.fillStyle = tint[0]; g.beginPath(); g.ellipse(cx, cy, r * 1.3, r, rnd(), 0, 6.3); g.fill();
      g.fillStyle = tint[1]; g.beginPath(); g.ellipse(cx - r * .3, cy - r * .2, r * .5, r * .35, rnd(), 0, 6.3); g.fill();
    }
    photos[seq] = c.toDataURL("image/jpeg", .8);
  }
  window.photoUrl = seq => photos[seq] || "";

  // Detections
  const det = [];
  function addResult(seq, label, conf, agree, at, low) {
    const rest = 100 - conf, p = [0, 0, 0, 0], i = KEYS.indexOf(label);
    const others = [0, 1, 2, 3].filter(k => k !== i), w = others.map(() => rnd() + .2), sw = w.reduce((a, b) => a + b);
    others.forEach((k, j) => p[k] = Math.round(rest * w[j] / sw)); p[i] = 100 - p.reduce((a, b) => a + b);
    photo(seq, label);
    det.unshift({ seq, label, conf: p[i], p, agree, ms: 278 + Math.round(rnd() * 22), low: low ? 1 : 0, at, img: 1 });
    if (det.length > 12) det.pop();
  }
  addResult(4, "HEALTHY", 91, 3, UP0 - 2400);
  addResult(5, "SALMONELLA", 47, 2, UP0 - 1500, true);
  addResult(6, "HEALTHY", 88, 3, UP0 - 760);
  addResult(7, "COCCIDIOSIS", 84, 3, UP0 - 130);
  let busy = 0, seq = 7;

  function capture() {
    busy = 1;
    setTimeout(() => {
      const r = rnd(), label = r < .4 ? "HEALTHY" : r < .65 ? "COCCIDIOSIS" : r < .85 ? "SALMONELLA" : "NEWCASTLE";
      const low = rnd() < .2, conf = low ? 42 + Math.round(rnd() * 15) : 72 + Math.round(rnd() * 25);
      addResult(++seq, label, conf, low ? 2 : 3, up(), low);
      busy = 0;
    }, 7000);
  }

  const json = o => Promise.resolve({ ok: true, json: () => Promise.resolve(o) });
  window.fetch = (url, opt) => {
    const s = up();
    if (s - lastHist >= 10) {
      lastHist = s; now = sensor(s);
      ["t", "h", "n"].forEach(k => { hist[k].push(now[k]); hist[k].shift(); });
    }
    if (url.startsWith("/api/state"))
      return json({ up: s, pi: 1, cam: 1, model: 1, busy, err: "", t: now.t, h: now.h, n: now.n, det });
    if (url.startsWith("/api/history")) return json(hist);
    if (url.startsWith("/api/capture")) {
      if (busy) return json({ ok: 0, why: "busy" });
      capture(); return json({ ok: 1 });
    }
    return Promise.reject(new Error("not simulated: " + url));
  };
})();
</script>
"""


def main():
    s = SRC.read_text()
    html = s[s.index('R"rawliteral(') + len('R"rawliteral('):s.index(')rawliteral"')]
    html = html.replace("<title>Poultry Disease Detector</title>",
                        "<title>Dashboard demo · Poultry Disease Detector</title>", 1)
    html = html.replace('<div class="wrap">', '<div class="wrap">' + BANNER, 1)
    i = html.index("<script>")
    html = html[:i] + SIM + html[i:]
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(html)
    print(f"wrote {OUT.relative_to(ROOT)} ({len(html)} bytes)")


if __name__ == "__main__":
    main()
