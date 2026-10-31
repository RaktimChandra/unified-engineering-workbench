// Task B: ASCII STL geometry and ANSYS nodal-temperature CSV visualisation.
// Parsing/validation happens in C++; this view only renders what it returns.
import { api, runJob, JobError, errorCard, esc, fmt, fmtBytes, fmtDuration } from "../api.js";
import { Viewer3D, buildMesh, THREE } from "../viewer3d.js";
import { viridis, paintColorbar } from "../colormap.js";

let root, viewer, examples = [];
let mode = "stl";
let model = { stl: null, csv: null };  // last successfully loaded model per mode
let currentJob = null;
let points = null, highlight = null;

export async function mount(el) {
  root = el;
  el.innerHTML = `
  <div class="view-head">
    <h1>Geometry &amp; imported results</h1>
    <span class="muted">Import an ASCII STL surface or an ANSYS nodal temperature CSV. Files are parsed and validated by the C++ backend; malformed input is rejected with the offending line.</span>
  </div>
  <div class="grid cols-sidebar">
    <div class="stack">
      <div class="card">
        <div class="row" role="radiogroup" aria-label="Data type">
          <button class="btn" data-mode="stl" role="radio">STL geometry</button>
          <button class="btn" data-mode="csv" role="radio">Nodal temperature CSV</button>
        </div>
        <div class="section-label" style="margin-top:12px">Bundled examples</div>
        <div class="row">
          <select id="g-example" aria-label="Bundled example file"></select>
          <button class="btn btn-primary" id="g-load-example">Load</button>
        </div>
        <p class="faint" id="g-example-desc" style="margin-top:6px"></p>
        <div class="section-label">Your file</div>
        <label class="file-drop" id="g-drop" tabindex="0">
          <input type="file" id="g-file">
          <span id="g-drop-text">Drop a file here or click to choose</span>
        </label>
        <div class="field" id="g-units-row" style="margin-top:10px">
          <label for="g-units">STL length unit</label>
          <select id="g-units"><option value="mm">millimetre</option><option value="m">metre</option><option value="in">inch</option></select>
          <span class="unit">STL has no unit</span>
        </div>
        <div class="row" id="g-busy" hidden>
          <div class="progress big"><div class="progress-fill" id="g-fill"></div></div>
          <button class="btn btn-small" id="g-cancel">Cancel</button>
        </div>
        <div id="g-msg"></div>
      </div>
      <div class="card" id="g-info"><p class="muted">No model loaded.</p></div>
    </div>
    <div class="stack">
      <div class="viewer" id="g-viewer">
        <div class="viewer-toolbar">
          <button class="btn btn-small" data-act="fit" title="Fit model to view (F)">Fit to view</button>
          <button class="btn btn-small" data-act="iso" title="Isometric view">Iso</button>
          <button class="btn btn-small" data-act="top" title="Look down -Z">Top</button>
          <button class="btn btn-small" data-act="front" title="Look along +Y">Front</button>
          <button class="btn btn-small" data-act="right" title="Look along -X">Right</button>
          <button class="btn btn-small" data-act="edges" id="g-edges-btn" title="Toggle feature edges">Edges</button>
          <label class="btn btn-small" id="g-size-wrap" hidden>Point size <input type="range" id="g-size" min="1" max="12" value="5" aria-label="Point size" style="vertical-align:middle;width:70px"></label>
          <button class="btn btn-small" data-act="png" title="Save the current view as PNG">PNG</button>
        </div>
        <div class="viewer-badge" id="g-badge"></div>
        <div class="viewer-empty" id="g-empty">Load a bundled example or drop a file to begin.</div>
        <div class="colorbar" id="g-colorbar" hidden>
          <div class="cb-title">T [°C]</div>
          <div class="cb-body"><canvas id="g-cb-canvas"></canvas><div class="cb-ticks" id="g-cb-ticks"></div></div>
        </div>
        <div class="inspect" id="g-inspect" hidden></div>
        <div class="viewer-hint">Left-drag rotate · right-drag / shift-drag pan · wheel zoom<span id="g-hint-extra"></span></div>
      </div>
    </div>
  </div>`;

  viewer = new Viewer3D(el.querySelector("#g-viewer"));
  paintColorbar(el.querySelector("#g-cb-canvas"));

  el.querySelectorAll("[data-mode]").forEach(b => b.addEventListener("click", () => setMode(b.dataset.mode)));
  el.querySelector("#g-load-example").addEventListener("click", () => {
    const name = el.querySelector("#g-example").value;
    if (name) importFile({ example: name });
  });
  el.querySelector("#g-example").addEventListener("change", describeExample);
  el.querySelector("#g-file").addEventListener("change", e => { const f = e.target.files[0]; if (f) importFile({ file: f }); e.target.value = ""; });
  const drop = el.querySelector("#g-drop");
  drop.addEventListener("keydown", e => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); el.querySelector("#g-file").click(); } });
  ["dragenter", "dragover"].forEach(t => drop.addEventListener(t, e => { e.preventDefault(); drop.classList.add("drag"); }));
  ["dragleave", "drop"].forEach(t => drop.addEventListener(t, e => { e.preventDefault(); drop.classList.remove("drag"); }));
  drop.addEventListener("drop", e => { const f = e.dataTransfer.files[0]; if (f) importFile({ file: f }); });
  el.querySelector("#g-cancel").addEventListener("click", () => currentJob?.cancel());
  el.querySelector(".viewer-toolbar").addEventListener("click", e => {
    const act = e.target.closest("[data-act]")?.dataset.act;
    if (!act) return;
    if (act === "fit") viewer.fit();
    else if (act === "png") savePng();
    else if (act === "edges") toggleEdges();
    else viewer.setView(act);
  });
  el.querySelector("#g-size").addEventListener("input", e => {
    if (points) { points.material.size = Number(e.target.value); viewer.render(); }
  });
  viewer.renderer.domElement.addEventListener("click", pick);
  window.addEventListener("keydown", e => {
    if (root.hidden || e.target.matches("input,select,textarea")) return;
    if (e.key === "f" || e.key === "F") viewer.fit();
  });

  try { examples = (await api.get("/api/examples")).examples || []; } catch { examples = []; }
  setMode("stl");
}

export function onShow() { viewer?.resize(); }

function setMode(m) {
  mode = m;
  root.querySelectorAll("[data-mode]").forEach(b => {
    const on = b.dataset.mode === m;
    b.classList.toggle("btn-primary", on);
    b.setAttribute("aria-checked", String(on));
  });
  root.querySelector("#g-units-row").hidden = m !== "stl";
  root.querySelector("#g-edges-btn").hidden = m !== "stl";
  root.querySelector("#g-size-wrap").hidden = m !== "csv";
  root.querySelector("#g-file").accept = m === "stl" ? ".stl,.STL" : ".csv,.CSV,.txt";
  root.querySelector("#g-drop-text").textContent = m === "stl"
    ? "Drop an ASCII .stl file here or click to choose"
    : "Drop a nodal CSV (node_id,x_m,y_m,z_m,temperature_C) or click to choose";
  root.querySelector("#g-hint-extra").textContent = m === "csv" ? " · click a node to inspect" : "";
  const sel = root.querySelector("#g-example");
  const list = examples.filter(x => x.kind === m);
  sel.innerHTML = list.length
    ? list.map(x => `<option value="${esc(x.name)}">${x.valid === false ? "[malformed] " : ""}${esc(x.name)}</option>`).join("")
    : `<option value="">(no bundled examples found)</option>`;
  describeExample();
  root.querySelector("#g-msg").innerHTML = "";
  render();
}

function describeExample() {
  const name = root.querySelector("#g-example").value;
  const ex = examples.find(x => x.name === name);
  root.querySelector("#g-example-desc").innerHTML = ex
    ? `${esc(ex.description || "")}${ex.provenance ? `<br><span class="faint">Provenance: ${esc(ex.provenance)}</span>` : ""}`
    : "";
}

async function importFile({ example, file }) {
  const msg = root.querySelector("#g-msg");
  msg.innerHTML = "";
  const units = root.querySelector("#g-units").value;
  const base = mode === "stl" ? "/api/geometry/import" : "/api/results/import";
  let url, body;
  if (example) {
    url = `${base}?example=${encodeURIComponent(example)}${mode === "stl" ? `&units=${units}` : ""}`;
  } else {
    url = `${base}?name=${encodeURIComponent(file.name)}${mode === "stl" ? `&units=${units}` : ""}`;
    body = await file.arrayBuffer();
  }
  const busy = root.querySelector("#g-busy"), fill = root.querySelector("#g-fill");
  busy.hidden = false; fill.style.width = "0%";
  const label = mode === "stl" ? "STL import" : "Nodal CSV import";
  const forMode = mode;
  currentJob = runJob(label, url, body, st => { fill.style.width = `${Math.round((st.progress || 0) * 100)}%`; });
  try {
    const { id, status } = await currentJob.promise;
    const blob = await api.blob(id);
    model[forMode] = { id, result: status.result, blob, elapsed: status.elapsed_ms };
    if (mode === forMode) render();
    const w = status.result.warnings || [];
    msg.innerHTML = `<div class="msg msg-ok"><p>Loaded <strong>${esc(status.result.file)}</strong> in ${fmtDuration(status.elapsed_ms)} (C++ parse + validation).</p></div>` +
      (w.length ? `<div class="msg msg-warn"><h3>${w.length} warning${w.length > 1 ? "s" : ""}</h3><ul class="plain">${w.map(x =>
        `<li>${esc(x.message)}${x.hint ? ` <span class="faint">${esc(x.hint)}</span>` : ""}</li>`).join("")}</ul></div>` : "");
  } catch (e) {
    if (e instanceof JobError && e.cancelled)
      msg.innerHTML = `<div class="msg msg-info"><p>Import cancelled. The previously loaded model (if any) is unchanged.</p></div>`;
    else
      msg.innerHTML = errorCard(e.info, mode === "stl" ? "STL file rejected" : "CSV file rejected") +
        (model[forMode] ? `<p class="faint">The previously loaded model is still displayed.</p>` : "");
  } finally {
    busy.hidden = true;
    currentJob = null;
  }
}

function render() {
  viewer.clear();
  points = null; highlight = null;
  root.querySelector("#g-inspect").hidden = true;
  root.querySelector("#g-colorbar").hidden = true;
  root.querySelector("#g-badge").innerHTML = "";
  const m = model[mode];
  root.querySelector("#g-empty").hidden = !!m;
  if (!m) { root.querySelector("#g-info").innerHTML = `<p class="muted">No ${mode === "stl" ? "geometry" : "nodal result"} loaded.</p>`; return; }
  if (mode === "stl") renderStl(m); else renderCsv(m);
  viewer.resetView();
}

function renderStl(m) {
  const r = m.result, s = r.stats, u = r.units;
  const nPos = r.layout.positions_f32, nNor = r.layout.normals_f32;
  const positions = new Float32Array(m.blob, 0, nPos);
  const normals = new Float32Array(m.blob, nPos * 4, nNor);
  const group = buildMesh(positions, normals);
  group.userData.edges.visible = edgesOn;
  viewer.add(group);
  const b = s.bounds, ext = [0, 1, 2].map(i => b.max[i] - b.min[i]);
  root.querySelector("#g-info").innerHTML = `
    <div class="card-head"><h3>${esc(r.file)}</h3><span class="spacer"></span>
      ${s.closed ? `<span class="badge badge-ok">closed</span>` : `<span class="badge badge-warn">open</span>`}</div>
    <table class="kv">
      <tr><th>Solid name</th><td>${esc(r.solid_name || "(none)")}</td></tr>
      <tr><th>File size</th><td>${fmtBytes(r.bytes)}</td></tr>
      <tr><th>Triangles</th><td>${s.triangles.toLocaleString("en-US")}</td></tr>
      <tr><th>Unique vertices</th><td>${s.unique_vertices.toLocaleString("en-US")}</td></tr>
      <tr><th>Boundary edges</th><td>${s.boundary_edges}</td></tr>
      <tr><th>Non-manifold edges</th><td>${s.non_manifold_edges}</td></tr>
      <tr><th>Degenerate facets</th><td>${s.degenerate}</td></tr>
      <tr><th>Flipped normals</th><td>${s.normal_mismatch}</td></tr>
      <tr><th>Surface area</th><td>${fmt(s.surface_area)} ${u}²</td></tr>
      <tr><th>Enclosed volume</th><td>${s.closed ? `${fmt(s.volume)} ${u}³` : "n/a (open mesh)"}</td></tr>
      <tr><th>Extent X × Y × Z</th><td>${ext.map(v => fmt(v, 5)).join(" × ")} ${u}</td></tr>
      <tr><th>Min corner</th><td>(${b.min.map(v => fmt(v, 5)).join(", ")}) ${u}</td></tr>
      <tr><th>Max corner</th><td>(${b.max.map(v => fmt(v, 5)).join(", ")}) ${u}</td></tr>
    </table>
    <p class="faint" style="margin-top:6px">Units are your selection (${u}); STL files store bare numbers. Grid spacing follows the model size.</p>`;
}

let edgesOn = true;
function toggleEdges() {
  edgesOn = !edgesOn;
  viewer.content.traverse(o => { if (o.isLineSegments) o.visible = edgesOn; });
  viewer.render();
}

function renderCsv(m) {
  const r = m.result, n = r.nodes;
  const xyz = new Float32Array(m.blob, 0, 3 * n);
  const T = new Float32Array(m.blob, 12 * n, n);
  const ids = new Float64Array(m.blob, 16 * n, n);
  const tmin = r.t_min_C, tmax = r.t_max_C, span = tmax - tmin || 1;
  const colors = new Float32Array(3 * n);
  for (let i = 0; i < n; i++) colors.set(viridis((T[i] - tmin) / span), 3 * i);
  const g = new THREE.BufferGeometry();
  g.setAttribute("position", new THREE.BufferAttribute(xyz, 3));
  g.setAttribute("color", new THREE.BufferAttribute(colors, 3));
  g.computeBoundingSphere();
  const mat = new THREE.PointsMaterial({ size: Number(root.querySelector("#g-size").value), vertexColors: true, sizeAttenuation: false });
  points = new THREE.Points(g, mat);
  points.userData = { ids, T, n };
  viewer.add(points);

  const hg = new THREE.BufferGeometry();
  hg.setAttribute("position", new THREE.BufferAttribute(new Float32Array(3), 3));
  highlight = new THREE.Points(hg, new THREE.PointsMaterial({ size: 16, color: 0xd1495b, sizeAttenuation: false, depthTest: false, transparent: true, opacity: 0.85 }));
  highlight.visible = false; highlight.renderOrder = 10;
  viewer.add(highlight);

  // colour bar
  const cb = root.querySelector("#g-colorbar"); cb.hidden = false;
  const ticks = root.querySelector("#g-cb-ticks");
  ticks.innerHTML = [0, 0.25, 0.5, 0.75, 1].map(f => `<span style="top:${(1 - f) * 100}%">${fmt(tmin + f * span, 4)}</span>`).join("");
  const synthetic = /SYNTHETIC/i.test(r.file);
  root.querySelector("#g-badge").innerHTML = synthetic ? `<span class="badge badge-sim" title="Self-generated test data, not an actual ANSYS export">SYNTHETIC DATA</span>` : "";
  const b = r.bounds_m;
  root.querySelector("#g-info").innerHTML = `
    <div class="card-head"><h3>${esc(r.file)}</h3><span class="spacer"></span>${synthetic ? `<span class="badge badge-sim">synthetic</span>` : ""}</div>
    <table class="kv">
      <tr><th>Nodes</th><td>${n.toLocaleString("en-US")}</td></tr>
      <tr><th>T min / mean / max</th><td>${fmt(tmin, 6)} / ${fmt(r.t_mean_C, 6)} / ${fmt(tmax, 6)} °C</td></tr>
      <tr><th>Min corner</th><td>(${b.min.map(v => fmt(v, 5)).join(", ")}) m</td></tr>
      <tr><th>Max corner</th><td>(${b.max.map(v => fmt(v, 5)).join(", ")}) m</td></tr>
      <tr><th>Schema</th><td>node_id, x_m, y_m, z_m, temperature_C</td></tr>
    </table>
    <p class="faint" style="margin-top:6px">Colour map: viridis (perceptually uniform), linear between min and max. Click a point to query its exact double-precision values from the C++ model.</p>`;
}

async function pick(ev) {
  if (mode !== "csv" || !points) return;
  const rect = ev.target.getBoundingClientRect();
  const mouse = new THREE.Vector2(((ev.clientX - rect.left) / rect.width) * 2 - 1, -((ev.clientY - rect.top) / rect.height) * 2 + 1);
  const ray = new THREE.Raycaster();
  ray.setFromCamera(mouse, viewer.camera);
  // Threshold in world units equivalent to ~6 px at the model centre.
  const dist = viewer.camera.position.distanceTo(viewer.controls.target);
  const worldPerPx = 2 * dist * Math.tan(THREE.MathUtils.degToRad(viewer.camera.fov / 2)) / rect.height;
  ray.params.Points.threshold = worldPerPx * 6;
  const hits = ray.intersectObject(points);
  const box = root.querySelector("#g-inspect");
  if (!hits.length) { box.hidden = true; highlight.visible = false; viewer.render(); return; }
  hits.sort((a, b) => a.distanceToRay - b.distanceToRay || a.distance - b.distance);
  const idx = hits[0].index;
  const p = points.geometry.attributes.position;
  highlight.geometry.attributes.position.setXYZ(0, p.getX(idx), p.getY(idx), p.getZ(idx));
  highlight.geometry.attributes.position.needsUpdate = true;
  highlight.geometry.computeBoundingSphere();
  highlight.visible = true;
  viewer.render();
  box.hidden = false;
  box.innerHTML = `<h4>Node ${points.userData.ids[idx]}</h4><p class="faint">querying C++ model…</p>`;
  try {
    const d = await api.get(`/api/results/${model.csv.id}/node/${idx}`);
    box.innerHTML = `<h4>Node ${d.node_id} <button class="btn-link" style="float:right" aria-label="Close node inspector" id="g-insp-x">close</button></h4>
      <table class="kv">
        <tr><th>x</th><td>${exact(d.x_m)} m</td></tr>
        <tr><th>y</th><td>${exact(d.y_m)} m</td></tr>
        <tr><th>z</th><td>${exact(d.z_m)} m</td></tr>
        <tr><th>T</th><td><strong>${exact(d.temperature_C)} °C</strong></td></tr>
        <tr><th>row index</th><td>${d.index}</td></tr>
      </table>`;
    box.querySelector("#g-insp-x").addEventListener("click", () => { box.hidden = true; highlight.visible = false; viewer.render(); });
  } catch (e) {
    box.innerHTML = errorCard(e.info, "Node query failed");
  }
}

const exact = v => String(Number(v.toPrecision(12)));

function savePng() {
  const a = document.createElement("a");
  a.href = viewer.screenshot();
  a.download = mode === "stl" ? "geometry_view.png" : "nodal_temperature_view.png";
  a.click();
}
