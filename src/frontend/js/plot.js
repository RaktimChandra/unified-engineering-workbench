// Minimal dependency-free SVG line/scatter plot with linear or log axes,
// labelled units, legend, hover readout and SVG export.
//
// linePlot(container, {
//   title, xLabel, yLabel, logX, logY, height,
//   series: [{ label, x:[], y:[], color, dashed, markers, line=true, width }],
//   xFmt, yFmt, filename
// })

const NS = "http://www.w3.org/2000/svg";
export const PALETTE = ["#d1495b", "#e09f3e", "#00798c", "#30638e", "#6a4c93", "#2b9348", "#8c564b"];

function el(name, attrs = {}, parent) {
  const e = document.createElementNS(NS, name);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
  if (parent) parent.appendChild(e);
  return e;
}

function niceTicks(lo, hi, target = 6) {
  if (!(hi > lo)) hi = lo + 1;
  const raw = (hi - lo) / target;
  const mag = Math.pow(10, Math.floor(Math.log10(raw)));
  const r = raw / mag;
  const step = (r < 1.5 ? 1 : r < 3.5 ? 2 : r < 7.5 ? 5 : 10) * mag;
  const out = [];
  for (let v = Math.ceil(lo / step - 1e-9) * step; v <= hi + step * 1e-9; v += step)
    out.push(Math.abs(v) < step * 1e-9 ? 0 : v);
  return out;
}

export function fmtTick(v, log) {
  if (log) {
    const e = Math.round(Math.log10(v));
    if (e >= -2 && e <= 3) return String(Number(v.toPrecision(3)));
    return `1e${e}`;
  }
  if (v === 0) return "0";
  const a = Math.abs(v);
  if (a >= 1e5 || a < 1e-3) return v.toExponential(1).replace("e+", "e");
  return String(Number(v.toPrecision(6)));
}

const defaultFmt = (v) => {
  if (!Number.isFinite(v)) return "–";
  const a = Math.abs(v);
  if (a !== 0 && (a >= 1e6 || a < 1e-3)) return v.toExponential(4).replace("e+", "e");
  return String(Number(v.toPrecision(7)));
};

export function linePlot(container, spec) {
  // Size the SVG to the container so text renders at its nominal pixel size;
  // re-render when the container width changes noticeably.
  const avail = Math.round(container.clientWidth || 0);
  const W = spec.width || Math.max(420, avail || 720);
  const H = spec.height || Math.round(Math.min(420, Math.max(270, W * 0.58)));
  if (!spec.width) {
    container._plotSpec = spec;
    container._plotW = W;
    if (!container._plotRO) {
      container._plotRO = new ResizeObserver(() => {
        const w = Math.round(container.clientWidth);
        if (w > 0 && Math.abs(w - container._plotW) > 24) linePlot(container, container._plotSpec);
      });
      container._plotRO.observe(container);
    }
  }
  container.innerHTML = "";
  container.classList.add("plot");
  const ml = 70, mr = 16, mt = spec.title ? 34 : 14, mb = 48;
  const pw = W - ml - mr, ph = H - mt - mb;
  const lx = spec.logX, ly = spec.logY;
  const tx = v => (lx ? Math.log10(v) : v), ty = v => (ly ? Math.log10(v) : v);
  const ok = (x, y) => Number.isFinite(x) && Number.isFinite(y) && (!lx || x > 0) && (!ly || y > 0);

  let x0 = Infinity, x1 = -Infinity, y0 = Infinity, y1 = -Infinity;
  for (const s of spec.series) for (let i = 0; i < s.x.length; i++) {
    if (!ok(s.x[i], s.y[i])) continue;
    x0 = Math.min(x0, tx(s.x[i])); x1 = Math.max(x1, tx(s.x[i]));
    y0 = Math.min(y0, ty(s.y[i])); y1 = Math.max(y1, ty(s.y[i]));
  }
  if (!Number.isFinite(x0)) { x0 = 0; x1 = 1; y0 = 0; y1 = 1; }
  if (spec.yMin !== undefined) y0 = ty(spec.yMin);
  if (spec.xMin !== undefined) x0 = tx(spec.xMin);
  if (spec.xMax !== undefined) x1 = tx(spec.xMax);
  if (lx) { x0 = Math.floor(x0); x1 = Math.ceil(x1); }
  if (ly) { y0 = Math.floor(y0); y1 = Math.ceil(y1); }
  else { const pad = 0.06 * ((y1 - y0) || Math.abs(y1) || 1); if (spec.yMin === undefined) y0 -= pad; y1 += pad; }
  if (x1 <= x0) x1 = x0 + 1;
  if (y1 <= y0) y1 = y0 + 1;
  const px = v => ml + (tx(v) - x0) / (x1 - x0) * pw;
  const py = v => mt + ph - (ty(v) - y0) / (y1 - y0) * ph;

  const svg = el("svg", { viewBox: `0 0 ${W} ${H}`, xmlns: NS, role: "img",
    "aria-label": spec.title || `${spec.yLabel} vs ${spec.xLabel}`, "font-family": "Segoe UI, Helvetica, Arial, sans-serif" });
  el("rect", { width: W, height: H, fill: "#ffffff" }, svg);
  if (spec.title) {
    const t = el("text", { x: ml, y: 21, "font-size": 13.5, "font-weight": 600, fill: "#1b1f24" }, svg);
    t.textContent = spec.title;
  }
  const grid = el("g", {}, svg);
  const xt = lx ? range(x0, x1).map(e => 10 ** e) : niceTicks(x0, x1, spec.xTicks || 7);
  const yt = ly ? range(y0, y1).map(e => 10 ** e) : niceTicks(y0, y1, 6);
  for (const v of xt) {
    if (tx(v) < x0 - 1e-9 || tx(v) > x1 + 1e-9) continue;
    const X = px(v);
    el("line", { x1: X, y1: mt, x2: X, y2: mt + ph, stroke: "#e8eaee" }, grid);
    const t = el("text", { x: X, y: mt + ph + 16, "font-size": 11, "text-anchor": "middle", fill: "#4b525c" }, grid);
    t.textContent = spec.xTickFmt ? spec.xTickFmt(v) : fmtTick(v, lx);
  }
  for (const v of yt) {
    if (ty(v) < y0 - 1e-9 || ty(v) > y1 + 1e-9) continue;
    const Y = py(v);
    el("line", { x1: ml, y1: Y, x2: ml + pw, y2: Y, stroke: "#e8eaee" }, grid);
    const t = el("text", { x: ml - 7, y: Y + 4, "font-size": 11, "text-anchor": "end", fill: "#4b525c" }, grid);
    t.textContent = fmtTick(v, ly);
  }
  el("rect", { x: ml, y: mt, width: pw, height: ph, fill: "none", stroke: "#9aa1ab" }, svg);
  const xl = el("text", { x: ml + pw / 2, y: H - 10, "font-size": 12, "text-anchor": "middle", fill: "#1b1f24" }, svg);
  xl.textContent = spec.xLabel || "";
  const yl = el("text", { transform: `translate(15,${mt + ph / 2}) rotate(-90)`, "font-size": 12, "text-anchor": "middle", fill: "#1b1f24" }, svg);
  yl.textContent = spec.yLabel || "";

  const clipId = `clip${Math.random().toString(36).slice(2, 8)}`;
  const defs = el("defs", {}, svg);
  const cp = el("clipPath", { id: clipId }, defs);
  el("rect", { x: ml, y: mt, width: pw, height: ph }, cp);
  const body = el("g", { "clip-path": `url(#${clipId})` }, svg);

  spec.series.forEach((s, si) => {
    const color = s.color || PALETTE[si % PALETTE.length];
    s._color = color;
    if (s.line !== false) {
      let d = "", pen = false;
      for (let i = 0; i < s.x.length; i++) {
        if (!ok(s.x[i], s.y[i])) { pen = false; continue; }
        d += `${pen ? "L" : "M"}${px(s.x[i]).toFixed(2)},${py(s.y[i]).toFixed(2)}`;
        pen = true;
      }
      el("path", { d, fill: "none", stroke: color, "stroke-width": s.width || 1.8,
        "stroke-dasharray": s.dashed ? "6 4" : "none", "stroke-linejoin": "round" }, body);
    }
    if (s.markers) for (let i = 0; i < s.x.length; i++) {
      if (!ok(s.x[i], s.y[i])) continue;
      el("circle", { cx: px(s.x[i]).toFixed(2), cy: py(s.y[i]).toFixed(2), r: s.markerSize || 3,
        fill: s.line === false ? color : "#fff", stroke: color, "stroke-width": 1.4 }, body);
    }
  });

  // legend
  const shown = spec.series.filter(s => s.label && !s.hideLegend);
  if (shown.length) {
    const lw = Math.min(pw - 10, Math.max(...shown.map(s => s.label.length)) * 6.3 + 44);
    const lh = 17 * shown.length + 8;
    const pos = spec.legend || "tr";
    const lxp = pos.includes("l") ? ml + 8 : ml + pw - lw - 8;
    const lyp = pos.includes("b") ? mt + ph - lh - 8 : mt + 8;
    const g = el("g", {}, svg);
    el("rect", { x: lxp, y: lyp, width: lw, height: lh, fill: "#fff", "fill-opacity": 0.93, stroke: "#cfd4db", rx: 3 }, g);
    shown.forEach((s, i) => {
      const y = lyp + 14 + 17 * i;
      if (s.line !== false)
        el("line", { x1: lxp + 8, y1: y - 4, x2: lxp + 30, y2: y - 4, stroke: s._color, "stroke-width": 2,
          "stroke-dasharray": s.dashed ? "6 4" : "none" }, g);
      if (s.markers) el("circle", { cx: lxp + 19, cy: y - 4, r: 3, fill: s.line === false ? s._color : "#fff", stroke: s._color }, g);
      const t = el("text", { x: lxp + 37, y, "font-size": 11, fill: "#1b1f24" }, g);
      t.textContent = s.label;
    });
  }
  container.appendChild(svg);

  // hover readout: nearest data point in screen space
  const tip = document.createElement("div");
  tip.className = "plot-tip"; tip.hidden = true;
  container.appendChild(tip);
  const cross = el("g", { "pointer-events": "none", visibility: "hidden" }, svg);
  const vline = el("line", { y1: mt, y2: mt + ph, stroke: "#5d6470", "stroke-dasharray": "3 3" }, cross);
  const dot = el("circle", { r: 4.5, fill: "none", stroke: "#1b1f24", "stroke-width": 1.5 }, cross);
  const xf = spec.xFmt || defaultFmt, yf = spec.yFmt || defaultFmt;
  svg.addEventListener("mousemove", ev => {
    const r = svg.getBoundingClientRect();
    const sx = (ev.clientX - r.left) * W / r.width, sy = (ev.clientY - r.top) * H / r.height;
    if (sx < ml || sx > ml + pw || sy < mt || sy > mt + ph) { tip.hidden = true; cross.setAttribute("visibility", "hidden"); return; }
    let best = null;
    spec.series.forEach(s => {
      if (s.noHover) return;
      for (let i = 0; i < s.x.length; i++) {
        if (!ok(s.x[i], s.y[i])) continue;
        const dx = px(s.x[i]) - sx, dy = py(s.y[i]) - sy, d = dx * dx + dy * dy;
        if (!best || d < best.d) best = { d, s, i };
      }
    });
    if (!best) return;
    const X = px(best.s.x[best.i]), Y = py(best.s.y[best.i]);
    vline.setAttribute("x1", X); vline.setAttribute("x2", X);
    dot.setAttribute("cx", X); dot.setAttribute("cy", Y);
    cross.setAttribute("visibility", "visible");
    tip.innerHTML = `<span class="sw" style="background:${best.s._color}"></span>${escapeHtml(best.s.label || "")}<br>` +
      `${escapeHtml(spec.xName || "x")} = ${xf(best.s.x[best.i])}<br>${escapeHtml(spec.yName || "y")} = ${yf(best.s.y[best.i])}`;
    tip.hidden = false;
    const cw = container.clientWidth;
    let left = X * r.width / W + 12, top = Y * r.height / H - 10;
    if (left > cw - 200) left = X * r.width / W - 200;
    tip.style.left = `${left}px`; tip.style.top = `${Math.max(0, top)}px`;
  });
  svg.addEventListener("mouseleave", () => { tip.hidden = true; cross.setAttribute("visibility", "hidden"); });

  // export
  const tools = document.createElement("div");
  tools.className = "plot-tools";
  const btn = document.createElement("button");
  btn.className = "btn btn-small"; btn.type = "button"; btn.textContent = "SVG";
  btn.title = "Download this plot as SVG"; btn.setAttribute("aria-label", "Download plot as SVG");
  btn.addEventListener("click", () => {
    const clone = svg.cloneNode(true);
    clone.querySelectorAll("[visibility]").forEach(n => n.remove());
    clone.setAttribute("width", W); clone.setAttribute("height", H);
    const blob = new Blob([new XMLSerializer().serializeToString(clone)], { type: "image/svg+xml" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = spec.filename || "plot.svg";
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 2000);
  });
  tools.appendChild(btn);
  container.appendChild(tools);
  return svg;
}

// Decade ticks; thinned so that at most ~8 labels are drawn on long log axes.
function range(a, b) {
  const lo = Math.round(a), hi = Math.round(b);
  const step = Math.max(1, Math.ceil((hi - lo) / 8));
  const o = [];
  for (let e = lo; e <= hi; e += step) o.push(e);
  return o;
}
function escapeHtml(s) { return String(s).replace(/[&<>]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" }[c])); }

/** Tiny inline sparkline (no axes) for telemetry. */
export function sparkline(values, color = "#1f5fbf", w = 80, h = 20) {
  if (values.length < 2) return `<svg viewBox="0 0 ${w} ${h}"></svg>`;
  const lo = Math.min(...values), hi = Math.max(...values), span = hi - lo || 1;
  const pts = values.map((v, i) => `${(i / (values.length - 1) * (w - 2) + 1).toFixed(1)},${(h - 2 - (v - lo) / span * (h - 4)).toFixed(1)}`).join(" ");
  return `<svg viewBox="0 0 ${w} ${h}" aria-hidden="true"><polyline points="${pts}" fill="none" stroke="${color}" stroke-width="1.4"/></svg>`;
}
