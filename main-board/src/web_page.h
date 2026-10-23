// web_page.h - the dashboard served at "/" (everything inline: the phone has no internet on the
// S3's access point). Polls /api/state every 2 s and /api/history every 10 s.
#pragma once
#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0b161f">
<title>Poultry Disease Detector</title>
<style>
:root{
  color-scheme:dark;
  --bg:#0b161f;--card:#102230;--card-2:#152b3c;--line:#21394b;
  --text:#f0efec;--text-2:#b6c2cb;--muted:#7f8e99;
  --brand:#8fe04a;
  --good:#0ca30c;--warning:#fab219;--critical:#d03b3b;
  --temp:#d95926;--hum:#3987e5;--nh3:#199e70;--bar:#3987e5;
  --r:16px;
}
*{box-sizing:border-box}
[hidden]{display:none!important}
html,body{margin:0;background:var(--bg);color:var(--text);
  font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
  -webkit-font-smoothing:antialiased}
body{background:radial-gradient(1200px 500px at 50% -200px,#16334a 0%,var(--bg) 60%) fixed}
.wrap{max-width:1040px;margin:0 auto;padding:16px 16px calc(24px + env(safe-area-inset-bottom))}
header{display:flex;align-items:center;gap:12px;margin:4px 0 14px}
.logo{width:44px;height:44px;border-radius:12px;flex:none;display:grid;place-items:center;
  background:linear-gradient(145deg,#1d3a50,#0f2232);border:1px solid var(--line)}
.title{flex:1;min-width:0}
.title h1{margin:0;font-size:17px;letter-spacing:.06em;font-weight:800;line-height:1.15}
.title h1 span{color:var(--brand)}
.title p{margin:2px 0 0;color:var(--muted);font-size:12.5px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.pill{display:inline-flex;align-items:center;gap:6px;padding:6px 10px;border-radius:999px;font-size:12px;
  color:var(--text-2);background:var(--card);border:1px solid var(--line);white-space:nowrap}
.dot{width:8px;height:8px;border-radius:50%;background:var(--muted);flex:none}
.live .dot{background:var(--good);box-shadow:0 0 0 0 rgba(12,163,12,.6);animation:pulse 2s infinite}
@keyframes pulse{70%{box-shadow:0 0 0 7px rgba(12,163,12,0)}100%{box-shadow:0 0 0 0 rgba(12,163,12,0)}}

.status{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:14px}
.chip{display:inline-flex;align-items:center;gap:7px;padding:7px 11px;border-radius:12px;font-size:12.5px;
  background:var(--card);border:1px solid var(--line);color:var(--text-2);white-space:nowrap}
.chip b{font-weight:600;color:var(--text)}
.ico{width:18px;height:18px;border-radius:50%;display:grid;place-items:center;font-size:11px;font-weight:900;color:#0b161f;flex:none}
.ok .ico{background:var(--good)} .bad .ico{background:var(--critical);color:#fff} .idle .ico{background:var(--muted)}

.grid{display:grid;gap:14px;grid-template-columns:1fr}
@media(min-width:760px){.grid{grid-template-columns:repeat(3,1fr)}.span3{grid-column:1/-1}.span2{grid-column:span 2}}
.card{background:linear-gradient(180deg,var(--card-2),var(--card));border:1px solid var(--line);border-radius:var(--r);padding:16px;min-width:0}
.card h2{margin:0 0 12px;font-size:12px;letter-spacing:.12em;text-transform:uppercase;color:var(--muted);font-weight:700;
  display:flex;align-items:center;justify-content:space-between;gap:8px}

/* AI result */
.ai{display:grid;gap:16px;grid-template-columns:1fr}
@media(min-width:620px){.ai{grid-template-columns:minmax(0,1.05fr) minmax(0,1fr)}}
.photo{position:relative;aspect-ratio:4/3;border-radius:12px;overflow:hidden;background:#081119;border:1px solid var(--line)}
.photo img{width:100%;height:100%;object-fit:cover;display:block}
.photo .empty{position:absolute;inset:0;display:grid;place-items:center;color:var(--muted);text-align:center;font-size:13px}
.photo .busy{position:absolute;inset:0;display:none;place-items:center;background:rgba(8,17,25,.72);backdrop-filter:blur(2px);text-align:center}
.photo.is-busy .busy{display:grid}
.spin{width:38px;height:38px;border-radius:50%;border:3px solid rgba(255,255,255,.15);border-top-color:var(--brand);animation:rot 1s linear infinite;margin:0 auto 10px}
@keyframes rot{to{transform:rotate(360deg)}}
.stamp{position:absolute;left:10px;bottom:10px;padding:4px 8px;border-radius:8px;font-size:11.5px;background:rgba(8,17,25,.75);color:var(--text-2)}
.verdict{display:flex;align-items:center;gap:14px}
.ring{position:relative;width:92px;height:92px;flex:none}
.ring svg{transform:rotate(-90deg)}
.ring .v{position:absolute;inset:0;display:grid;place-items:center;text-align:center}
.ring .v b{font-size:24px;line-height:1;font-weight:800}
.ring .v small{display:block;font-size:9px;color:var(--muted);letter-spacing:.04em;margin-top:2px}
.label{font-size:22px;font-weight:800;line-height:1.15;margin:0}
.badge{display:inline-flex;align-items:center;gap:6px;margin-top:6px;padding:4px 9px;border-radius:999px;font-size:12px;font-weight:600;
  border:1px solid var(--line);background:rgba(255,255,255,.04);color:var(--text)}
.badge i{font-style:normal;width:16px;height:16px;border-radius:50%;display:grid;place-items:center;font-size:10px;font-weight:900;color:#0b161f}
.b-good i{background:var(--good)} .b-crit i{background:var(--critical);color:#fff} .b-warn i{background:var(--warning)}
.b-good{border-color:rgba(12,163,12,.55)} .b-crit{border-color:rgba(208,59,59,.6)} .b-warn{border-color:rgba(250,178,25,.6)}
.advice{margin:12px 0 0;color:var(--text-2);font-size:13.5px}
.probs{margin-top:14px;display:grid;gap:9px}
.prow{display:grid;grid-template-columns:96px 1fr 40px;align-items:center;gap:10px;font-size:13px;color:var(--text-2)}
.prow.top{color:var(--text);font-weight:600}
.track{height:10px;border-radius:5px;background:rgba(255,255,255,.06);overflow:hidden}
.fill{height:100%;border-radius:5px;background:var(--bar);opacity:.45;width:0;transition:width .6s ease}
.prow.top .fill{opacity:1}
.prow .pct{text-align:right;font-variant-numeric:tabular-nums}
.meta{display:flex;flex-wrap:wrap;gap:8px;margin-top:14px}
.meta span{font-size:12.5px;color:var(--text-2);padding:5px 9px;border-radius:9px;background:rgba(255,255,255,.04);border:1px solid var(--line)}
.agree{display:inline-flex;gap:3px;margin-right:4px;vertical-align:-1px}
.agree em{width:9px;height:9px;border-radius:50%;background:rgba(255,255,255,.18)}
.agree em.on{background:var(--text-2)}
.btn{margin-top:16px;width:100%;padding:14px 16px;border-radius:12px;border:0;font:inherit;font-weight:800;letter-spacing:.04em;
  color:#0b161f;background:linear-gradient(180deg,#a6ef62,var(--brand));cursor:pointer;display:flex;align-items:center;justify-content:center;gap:10px}
.btn:active{transform:translateY(1px)}
.btn[disabled]{background:#2a3d4c;color:var(--muted);cursor:not-allowed}
.note{min-height:18px;margin:8px 0 0;font-size:12.5px;color:var(--text-2);text-align:center}

/* sensors */
.sensor .val{display:flex;align-items:baseline;gap:6px;margin:-2px 0 8px}
.sensor .val b{font-size:34px;font-weight:800;letter-spacing:-.01em;font-variant-numeric:tabular-nums}
.sensor .val span{color:var(--muted);font-size:14px}
.key{display:inline-block;width:10px;height:10px;border-radius:3px;margin-right:6px;vertical-align:0}
.chart{position:relative;min-height:88px}
.chart svg{width:100%;height:70px;display:block;overflow:visible;touch-action:pan-y}
.chart .ax{display:flex;justify-content:space-between;font-size:11px;color:var(--muted);margin-top:4px}
.tip{position:absolute;top:-6px;transform:translate(-50%,-100%);pointer-events:none;padding:5px 8px;border-radius:8px;font-size:12px;white-space:nowrap;
  background:#0a141c;border:1px solid var(--line);color:var(--text);display:none;box-shadow:0 6px 18px rgba(0,0,0,.35)}
.tip small{color:var(--muted)}
.range{font-size:11px;color:var(--muted);text-transform:none;letter-spacing:0;font-weight:500}

/* history */
.hist{display:grid;gap:10px;grid-template-columns:repeat(auto-fill,minmax(150px,1fr))}
.h-item{border-radius:12px;overflow:hidden;background:#0c1b26;border:1px solid var(--line)}
.h-item .im{aspect-ratio:4/3;background:#081119;display:grid;place-items:center;color:var(--muted);font-size:11px}
.h-item img{width:100%;height:100%;object-fit:cover;display:block}
.h-item .tx{padding:8px 10px;font-size:12.5px}
.h-item .tx b{display:flex;align-items:center;gap:6px}
.h-item .tx b i{width:8px;height:8px;border-radius:50%;flex:none}
.h-item .tx small{color:var(--muted)}
.none{color:var(--muted);font-size:13px;margin:0}
footer{margin-top:18px;color:var(--muted);font-size:12px;display:flex;flex-wrap:wrap;gap:6px 16px;justify-content:center}
</style>
</head>
<body>
<div class="wrap">
  <header>
    <div class="logo" aria-hidden="true">
      <svg width="26" height="26" viewBox="0 0 24 24" fill="none" stroke="#8fe04a" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
        <path d="M7 14c0-4 2.5-8 6-8 2.2 0 3.5 1.6 3.5 3.3 0 1-.4 1.7-1 2.3 1.6.6 3.5 2.2 3.5 4.9C19 19 16.3 21 12 21s-5-2.6-5-7z"/>
        <path d="M16.5 7.5l2-1.2-1.6-.6M14 10h.01"/>
      </svg>
    </div>
    <div class="title">
      <h1>POULTRY <span>DISEASE DETECTOR</span></h1>
      <p>Smart health monitoring for better poultry birds</p>
    </div>
    <div class="pill" id="conn"><i class="dot"></i><span>Connecting…</span></div>
  </header>

  <div class="status" id="status" aria-label="System status"></div>

  <div class="grid">
    <section class="card span3" aria-labelledby="ai-h">
      <h2 id="ai-h">AI disease classification <span class="range" id="ai-when"></span></h2>
      <div class="ai">
        <div>
          <div class="photo" id="photo">
            <img id="photo-img" alt="Last photo of the droppings" hidden>
            <div class="empty" id="photo-empty">
              <div><svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M4 8h3l2-3h6l2 3h3v11H4z"/><circle cx="12" cy="13" r="4"/></svg>
              <div>No photo yet — press <b>Capture</b><br>or the button on the detector</div></div>
            </div>
            <div class="busy"><div><div class="spin"></div><b>Capturing &amp; analysing…</b><div class="note" style="margin:4px 0 0">camera → 3 AI models</div></div></div>
            <div class="stamp" id="photo-stamp" hidden></div>
          </div>
          <button class="btn" id="cap" type="button">
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M4 8h3l2-3h6l2 3h3v11H4z"/><circle cx="12" cy="13" r="3.5"/></svg>
            <span>CAPTURE &amp; ANALYSE</span>
          </button>
          <p class="note" id="cap-note"></p>
        </div>
        <div>
          <div class="verdict">
            <div class="ring" aria-hidden="true">
              <svg width="92" height="92" viewBox="0 0 92 92">
                <circle cx="46" cy="46" r="38" fill="none" stroke="rgba(255,255,255,.08)" stroke-width="9"/>
                <circle id="ring-arc" cx="46" cy="46" r="38" fill="none" stroke="var(--muted)" stroke-width="9" stroke-linecap="round"
                  stroke-dasharray="0 239" style="transition:stroke-dasharray .4s ease"/>
              </svg>
              <div class="v"><div><b id="conf">--</b><small>CONFIDENCE</small></div></div>
            </div>
            <div style="min-width:0">
              <p class="label" id="label">Waiting for a result</p>
              <span class="badge" id="badge" hidden></span>
            </div>
          </div>
          <p class="advice" id="advice">The detector photographs the droppings, three AI models classify them and agree on a verdict.</p>
          <div class="probs" id="probs" aria-label="Probability per class"></div>
          <div class="meta" id="meta"></div>
        </div>
      </div>
    </section>

    <section class="card sensor" aria-labelledby="t-h">
      <h2 id="t-h"><span><i class="key" style="background:var(--temp)"></i>Temperature</span><span class="range">last 15 min</span></h2>
      <div class="val"><b id="t-v">--</b><span>°C</span></div>
      <div class="chart" id="t-c"></div>
    </section>
    <section class="card sensor" aria-labelledby="h-h">
      <h2 id="h-h"><span><i class="key" style="background:var(--hum)"></i>Humidity</span><span class="range">last 15 min</span></h2>
      <div class="val"><b id="h-v">--</b><span>%RH</span></div>
      <div class="chart" id="h-c"></div>
    </section>
    <section class="card sensor" aria-labelledby="n-h">
      <h2 id="n-h"><span><i class="key" style="background:var(--nh3)"></i>Ammonia (MQ135)</span><span class="range">last 15 min</span></h2>
      <div class="val"><b id="n-v">--</b><span>ppm NH₃ (est.)</span></div>
      <div class="chart" id="n-c"></div>
    </section>

    <section class="card span3" aria-labelledby="hist-h">
      <h2 id="hist-h">Recent detections</h2>
      <div class="hist" id="hist"><p class="none">No detections yet.</p></div>
    </section>
  </div>

  <footer>
    <span>Device ID: NL_PDD_0126</span>
    <span id="uptime">Uptime --</span>
    <span>WiFi: PoultryDetector · 192.168.4.1</span>
  </footer>
</div>

<script>
const $ = id => document.getElementById(id);
const CLASSES = [["Coccidiosis","COCCIDIOSIS"],["Healthy","HEALTHY"],["Newcastle","NEWCASTLE"],["Salmonella","SALMONELLA"]];
const NAMES = {COCCIDIOSIS:"Coccidiosis",HEALTHY:"Healthy",NEWCASTLE:"Newcastle disease",SALMONELLA:"Salmonella"};
const ADVICE = {
  HEALTHY:"No disease signs in this sample. Keep monitoring litter, feed and water hygiene.",
  COCCIDIOSIS:"Signs of coccidiosis. Check litter moisture, review anticoccidial treatment and confirm with a vet.",
  NEWCASTLE:"Possible Newcastle disease, which is notifiable. Isolate the flock and contact a vet immediately.",
  SALMONELLA:"Possible salmonella. Separate affected birds, tighten hygiene and confirm with a lab test."
};
let state = null, textSeq = -1, photoSeq = -1, histKey = "", fails = 0;
// Photo URL of a detection (the GitHub Pages demo supplies its own, see tools/build_demo.py)
const photoUrl = window.photoUrl || ((seq, at) => "/photo?seq=" + seq + "&at=" + at);

function ago(s){ if(s<10) return "just now"; if(s<60) return s+" s ago"; if(s<3600) return Math.floor(s/60)+" min ago";
  return Math.floor(s/3600)+" h "+Math.floor(s%3600/60)+" min ago"; }
function hms(s){ const h=Math.floor(s/3600), m=Math.floor(s%3600/60), x=s%60; return (h?h+"h ":"")+m+"m "+x+"s"; }
function kind(d){ if(d.low) return "warn"; return d.label==="HEALTHY" ? "good" : "crit"; }
function badgeHtml(d){
  const k = kind(d), icon = k==="good" ? "✓" : "!";
  const text = d.low ? "Low confidence – re-capture" : (d.label==="HEALTHY" ? "No disease detected" : "Disease detected");
  return `<i>${icon}</i>${text}`;
}
function chip(name, ok, extra){
  const cls = ok===null ? "idle" : ok ? "ok" : "bad";
  const icon = ok===null ? "–" : ok ? "✓" : "✕";
  return `<span class="chip ${cls}"><span class="ico">${icon}</span><b>${name}</b>${extra||(ok===null?"":ok?"online":"offline")}</span>`;
}

function renderStatus(s, online){
  const pi = online && !!s.pi;
  $("status").innerHTML =
    chip("Main board", online, online ? "online" : "unreachable") +
    chip("Raspberry Pi", online ? pi : null) +
    chip("Camera", online ? (pi && !!s.cam) : null, online ? (pi && s.cam ? "ready" : "not ready") : "") +
    chip("AI models", online ? (pi && !!s.model) : null, online ? (pi && s.model ? "loaded" : "not loaded") : "") +
    chip("Sensors", online ? (s.t!==null || s.n!==null) : null, online ? ((s.t!==null) + (s.n!==null) + "/2 reading") : "");
}

function renderSensors(s){
  $("t-v").textContent = s.t===null ? "--" : s.t.toFixed(1);
  $("h-v").textContent = s.h===null ? "--" : Math.round(s.h);
  $("n-v").textContent = s.n===null ? "--" : s.n.toFixed(1);
}

function renderAI(s){
  const d = s.det[0];
  const photo = $("photo");
  photo.classList.toggle("is-busy", !!s.busy);
  const cap = $("cap");
  cap.disabled = !s.pi || !!s.busy;
  cap.querySelector("span").textContent = s.busy ? "ANALYSING…" : (!s.pi ? "DETECTOR OFFLINE" : "CAPTURE & ANALYSE");
  if (s.err) $("cap-note").textContent = s.err==="CAM" ? "Camera error on the last capture" : "Error on the last capture ("+s.err+")";
  if (!d) return;
  $("ai-when").textContent = "#"+d.seq+" · "+ago(s.up - d.at);
  $("photo-stamp").textContent = "#"+d.seq+" · "+ago(s.up - d.at);
  if (d.img && d.seq !== photoSeq) {   // the photo arrives ~1 s after the result
    photoSeq = d.seq;
    const img = $("photo-img");
    img.onload = () => { img.hidden = false; $("photo-empty").hidden = true; $("photo-stamp").hidden = false; };
    img.src = photoUrl(d.seq, d.at);
  }
  if (d.seq === textSeq) return;
  textSeq = d.seq;
  const k = kind(d);
  const col = k==="good" ? "var(--good)" : k==="warn" ? "var(--warning)" : "var(--critical)";
  $("ring-arc").setAttribute("stroke", col);
  $("ring-arc").setAttribute("stroke-dasharray", (2.387*d.conf).toFixed(1)+" 239");
  $("conf").textContent = d.conf+"%";
  $("label").textContent = NAMES[d.label] || d.label;
  const b = $("badge"); b.className = "badge b-"+k; b.innerHTML = badgeHtml(d); b.hidden = false;
  $("advice").textContent = (d.low ? "The models are unsure about this photo – take another one. " : "") + (ADVICE[d.label]||"");
  const top = d.p.indexOf(Math.max(...d.p));
  $("probs").innerHTML = CLASSES.map((c,i)=>
    `<div class="prow${i===top?" top":""}"><span>${c[0]}</span><div class="track"><div class="fill" data-w="${d.p[i]}"></div></div><span class="pct">${d.p[i]}%</span></div>`).join("");
  requestAnimationFrame(()=>document.querySelectorAll(".fill").forEach(f=>f.style.width=f.dataset.w+"%"));
  const dots = [0,1,2].map(i=>`<em class="${i<d.agree?"on":""}"></em>`).join("");
  $("meta").innerHTML = `<span><span class="agree">${dots}</span>${d.agree}/3 models agree</span><span>⚡ ${d.ms} ms inference</span><span>Result #${d.seq}</span>`;
}

function renderHistory(s){
  const key = s.det.map(d=>d.seq+":"+d.img).join(",");
  if (key === histKey) {   // only refresh the "x min ago"
    document.querySelectorAll("[data-at]").forEach(e=>e.textContent = ago(s.up - +e.dataset.at));
    return;
  }
  histKey = key;
  if (!s.det.length) { $("hist").innerHTML = '<p class="none">No detections yet.</p>'; return; }
  $("hist").innerHTML = s.det.map(d=>{
    const k = kind(d), col = k==="good" ? "var(--good)" : k==="warn" ? "var(--warning)" : "var(--critical)";
    return `<div class="h-item"><div class="im">${d.img ? `<img loading="lazy" alt="Photo #${d.seq}" src="${photoUrl(d.seq, d.at)}">` : "no photo"}</div>
      <div class="tx"><b><i style="background:${col}"></i>${NAMES[d.label]||d.label}</b>
      <small>${d.conf}%${d.low?" · low":""} · #${d.seq} · <span data-at="${d.at}">${ago(s.up-d.at)}</span></small></div></div>`;
  }).join("");
}

// ---- single-series trend chart with crosshair + tooltip
function chart(el, data, color, unit, digits, every){
  const W = 300, H = 70, P = 4;
  const pts = data.map((v,i)=>[i,v]).filter(p=>p[1]!==null);
  if (pts.length < 2) { el.innerHTML = '<p class="none" style="padding-top:26px;text-align:center">Collecting data…</p>'; return; }
  let lo = Math.min(...pts.map(p=>p[1])), hi = Math.max(...pts.map(p=>p[1]));
  if (hi - lo < 1) { const m = (hi+lo)/2; lo = m-0.5; hi = m+0.5; }
  const n = data.length, x = i => P + (W-2*P) * (n<2 ? 1 : i/(n-1)), y = v => H-P - (H-2*P) * (v-lo)/(hi-lo);
  // Gaps (failed reads) split the line; each run gets its own area fill
  const runs = []; let cur = null;
  data.forEach((v,i)=>{ if (v===null) { cur = null; return; } if (!cur) runs.push(cur = []); cur.push(i); });
  let line = "", area = "";
  runs.forEach(r=>{
    const seg = r.map((i,k)=>(k?"L":"M")+x(i).toFixed(1)+" "+y(data[i]).toFixed(1)).join("");
    line += seg;
    area += seg + `L${x(r[r.length-1]).toFixed(1)} ${H} L${x(r[0]).toFixed(1)} ${H}Z`;
  });
  const gid = "g"+el.id;
  el.innerHTML = `<svg viewBox="0 0 ${W} ${H}" preserveAspectRatio="none" role="img" aria-label="trend">
    <defs><linearGradient id="${gid}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="${color}" stop-opacity=".28"/><stop offset="1" stop-color="${color}" stop-opacity="0"/></linearGradient></defs>
    <line x1="0" x2="${W}" y1="${H-P}" y2="${H-P}" stroke="rgba(255,255,255,.08)" vector-effect="non-scaling-stroke"/>
    <path d="${area}" fill="url(#${gid})"/>
    <path d="${line}" fill="none" stroke="${color}" stroke-width="2" stroke-linejoin="round" stroke-linecap="round" vector-effect="non-scaling-stroke"/>
    <line class="cx" x1="0" x2="0" y1="0" y2="${H}" stroke="rgba(255,255,255,.35)" vector-effect="non-scaling-stroke" visibility="hidden"/>
  </svg><div class="tip"></div>
  <div class="ax"><span>${Math.round((n-1)*every/60)} min ago</span><span>min ${lo.toFixed(digits)} · max ${hi.toFixed(digits)} ${unit}</span><span>now</span></div>`;
  const svg = el.querySelector("svg"), cx = el.querySelector(".cx"), tip = el.querySelector(".tip");
  const dot = document.createElement("div");
  dot.style.cssText = `position:absolute;width:10px;height:10px;border-radius:50%;background:${color};border:2px solid #0b161f;transform:translate(-50%,-50%);display:none;pointer-events:none`;
  el.appendChild(dot);
  const move = e => {
    const r = svg.getBoundingClientRect();
    const i = Math.max(0, Math.min(n-1, Math.round(((e.clientX - r.left)/r.width*W - P)/(W-2*P)*(n-1))));
    const v = data[i];
    if (v===null) { tip.style.display = dot.style.display = "none"; cx.setAttribute("visibility","hidden"); return; }
    const px = x(i)/W*r.width, py = y(v)/H*r.height;
    cx.setAttribute("x1", x(i)); cx.setAttribute("x2", x(i)); cx.setAttribute("visibility","visible");
    dot.style.left = px+"px"; dot.style.top = py+"px"; dot.style.display = "block";
    const secs = (n-1-i)*every;
    tip.innerHTML = `<b>${v.toFixed(digits)} ${unit}</b> <small>${secs ? ago(secs) : "now"}</small>`;
    tip.style.left = Math.max(40, Math.min(r.width-40, px))+"px"; tip.style.display = "block";
  };
  const leave = () => { tip.style.display = dot.style.display = "none"; cx.setAttribute("visibility","hidden"); };
  svg.addEventListener("pointermove", move); svg.addEventListener("pointerdown", move);
  svg.addEventListener("pointerleave", leave);
}

async function poll(){
  try {
    const r = await fetch("/api/state", {cache:"no-store"});
    state = await r.json(); fails = 0;
    $("conn").className = "pill live"; $("conn").lastChild.textContent = "Live";
    renderStatus(state, true); renderSensors(state); renderAI(state); renderHistory(state);
    $("uptime").textContent = "Uptime " + hms(state.up);
  } catch (e) {
    if (++fails >= 2) { $("conn").className = "pill"; $("conn").lastChild.textContent = "Reconnecting…"; renderStatus({}, false); }
  }
}
async function pollHistory(){
  try {
    const h = await (await fetch("/api/history", {cache:"no-store"})).json();
    chart($("t-c"), h.t, "#d95926", "°C", 1, h.every);
    chart($("h-c"), h.h, "#3987e5", "%", 0, h.every);
    chart($("n-c"), h.n, "#199e70", "ppm", 1, h.every);
  } catch (e) {}
}
$("cap").addEventListener("click", async () => {
  const b = $("cap"); b.disabled = true; $("cap-note").textContent = "Sending capture request…";
  try {
    const r = await fetch("/api/capture", {method:"POST"}); const j = await r.json();
    $("cap-note").textContent = j.ok ? "Capturing – the result appears here in about 20 s" : (j.why==="busy" ? "Already analysing a photo" : "The detector is offline");
  } catch (e) { $("cap-note").textContent = "Could not reach the main board"; }
  setTimeout(poll, 400);
});
poll(); pollHistory();
setInterval(poll, 2000); setInterval(pollHistory, 10000);
</script>
</body>
</html>
)rawliteral";
