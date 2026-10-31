// Task C: 1-D steady thermal FEA (two-node linear elements), verification
// against the closed-form solution and mesh convergence.
import { api, runJob, JobError, errorCard, exportLinks, esc, fmt } from "../api.js";
import { linePlot, PALETTE } from "../plot.js";

const DEFAULTS = { length: 1, area: 1e-4, conductivity: 10, heat_gen: 1e4, t_left: 100, t_right: 20 };
const FIELDS = [
  ["length", "Length L", "m", v => v > 0 || "Length L must be a finite value > 0 m."],
  ["area", "Area A", "m²", v => v > 0 || "Cross-section area A must be a finite value > 0 m^2."],
  ["conductivity", "Conductivity k", "W/(m·K)", v => v > 0 || "Conductivity k must be a finite value > 0 W/(m K)."],
  ["heat_gen", "Heat generation q", "W/m³", () => true],
  ["t_left", "T(0)", "°C", v => v >= -273.15 || "Boundary temperatures must be above absolute zero (-273.15 C)."],
  ["t_right", "T(L)", "°C", v => v >= -273.15 || "Boundary temperatures must be above absolute zero (-273.15 C)."],
];
const MESHES = [10, 20, 40, 80];

let root, job = null;

export async function mount(el) {
  root = el;
  el.innerHTML = `
  <div class="view-head">
    <h1>Thermal analysis: 1-D steady conduction FEA</h1>
    <span class="muted">Uniform rod, two-node linear elements, prescribed temperatures at both ends, insulated lateral surface. Solved in C++; plots and CSV come from the same run.</span>
  </div>
  <div class="grid cols-sidebar">
    <div class="stack">
      <div class="card">
        <div class="card-head"><h3>Problem</h3><span class="spacer"></span>
          <button class="btn btn-small" id="t-reset" title="Restore the Task C parameters">Reset to Task C defaults</button></div>
        <form id="t-form" novalidate>
          ${FIELDS.map(([k, label, unit]) => `
            <div class="field" data-f="${k}"><label for="t-${k}">${label}</label>
              <input type="number" step="any" id="t-${k}" name="${k}" value="${DEFAULTS[k]}"><span class="unit">${unit}</span></div>
            <div class="field-msg" id="t-${k}-msg" hidden></div>`).join("")}
          <div class="section-label">Mesh study</div>
          <div class="checks" id="t-meshes">${MESHES.map(n => `<label><input type="checkbox" value="${n}" checked> N = ${n}</label>`).join("")}</div>
          <div class="field" style="margin-top:6px"><label for="t-extra">Extra meshes</label>
            <input type="text" id="t-extra" placeholder="e.g. 160, 320"><span class="unit">elements</span></div>
          <div class="field-msg" id="t-extra-msg" hidden></div>
          <div class="row" style="margin-top:8px">
            <button class="btn btn-primary" type="submit" id="t-run">Run convergence study</button>
            <button class="btn" type="button" id="t-cancel" hidden>Cancel</button>
          </div>
          <div class="section-label">Single solve</div>
          <div class="field"><label for="t-n">Elements N</label><input type="number" id="t-n" value="4" min="1" step="1"><span class="unit">1..1e6</span></div>
          <div class="field-msg" id="t-n-msg" hidden></div>
          <button class="btn" type="button" id="t-single">Solve and show assembled system</button>
        </form>
        <div id="t-msg"></div>
      </div>
      <div class="card">
        <h3>Formulation</h3>
        <div class="eq">−d/dx( k A dT/dx ) = q A,   0 &lt; x &lt; L
T(0) = T₀,  T(L) = T_L   (Dirichlet)
Element e, length h = L/N:
  kₑ = (kA/h)·[1 −1; −1 1]   [W/K]
  fₑ = (qAh/2)·[1; 1]          [W]
Assemble K T = F; impose T₀, T_L by
symmetric elimination; solve (Thomas).
Exact:
  T(x) = T₀ + (T_L−T₀) x/L
         + q/(2k) · x (L−x)</div>
        <p class="faint" style="margin-top:8px">Units: SI throughout; temperatures in °C. Because the equation is linear in T, using °C or K gives identical gradients and fluxes.</p>
      </div>
      <div class="note"><strong>Debugging insight.</strong> Comparing FE and exact values only at the nodes gives errors of ~1e-13 °C for every N: it looks like a perfect solver and shows no convergence at all. In 1-D, linear elements with the consistent load vector are <em>nodally exact</em> (the Green's function lies in the FE space), so nodal error says nothing about discretisation error. The real error lives between nodes: at element midpoints it equals q h²/(8k) = 125 h² °C, i.e. 1.25 °C at N = 10 and halving h divides it by 4. The study therefore reports midpoint and L2 errors, and the unit tests assert both properties.</div>
    </div>
    <div class="stack">
      <div class="kpis" id="t-kpis"></div>
      <div class="grid cols-2">
        <div class="card"><div id="t-plot-T"></div></div>
        <div class="card"><div id="t-plot-err"></div></div>
      </div>
      <div class="grid cols-2">
        <div class="card"><div id="t-plot-conv"></div></div>
        <div class="card">
          <h3>Convergence table</h3>
          <div id="t-exports" style="margin-bottom:8px"></div>
          <div class="table-wrap" id="t-table"><p class="muted">Run the study.</p></div>
          <div id="t-balance" style="margin-top:8px"></div>
        </div>
      </div>
      <div class="card" id="t-single-out" hidden></div>
    </div>
  </div>`;

  el.querySelector("#t-form").addEventListener("submit", e => { e.preventDefault(); runStudy(); });
  el.querySelector("#t-reset").addEventListener("click", () => {
    for (const [k] of FIELDS) el.querySelector(`#t-${k}`).value = DEFAULTS[k];
    el.querySelectorAll("#t-meshes input").forEach(c => (c.checked = true));
    el.querySelector("#t-extra").value = "";
    validate(); runStudy();
  });
  el.querySelector("#t-cancel").addEventListener("click", () => job?.cancel());
  el.querySelector("#t-single").addEventListener("click", solveSingle);
  el.querySelectorAll("#t-form input").forEach(i => i.addEventListener("input", () => validate()));
  runStudy();
}

function showMsg(id, text) {
  const m = root.querySelector(`#${id}-msg`);
  if (!m) return;
  m.hidden = !text; m.textContent = text || "";
  m.previousElementSibling?.classList.toggle("invalid", !!text);
}

/** Mirrors RodProblem::validate() so errors appear before a request is sent. */
function validate() {
  const p = {}; let ok = true;
  for (const [k, , , rule] of FIELDS) {
    const raw = root.querySelector(`#t-${k}`).value.trim();
    const v = Number(raw);
    let err = "";
    if (raw === "" || !Number.isFinite(v)) err = "Enter a finite number.";
    else { const r = rule(v); if (r !== true) err = r; }
    showMsg(`t-${k}`, err);
    if (err) ok = false; else p[k] = v;
  }
  return ok ? p : null;
}

function meshes() {
  const list = [...root.querySelectorAll("#t-meshes input:checked")].map(c => Number(c.value));
  const extra = root.querySelector("#t-extra").value.trim();
  let err = "";
  if (extra) for (const tok of extra.split(/[\s,;]+/).filter(Boolean)) {
    const n = Number(tok);
    if (!Number.isInteger(n) || n < 1 || n > 200000) { err = `'${tok}' is not an integer between 1 and 200000.`; break; }
    list.push(n);
  }
  showMsg("t-extra", err);
  if (err) return null;
  const uniq = [...new Set(list)].sort((a, b) => a - b);
  if (!uniq.length) { showMsg("t-extra", "Select at least one mesh."); return null; }
  if (uniq.length > 12) { showMsg("t-extra", "At most 12 meshes per study."); return null; }
  return uniq;
}

async function runStudy() {
  const problem = validate(), m = meshes();
  const msg = root.querySelector("#t-msg");
  msg.innerHTML = "";
  if (!problem || !m) { msg.innerHTML = `<div class="msg msg-warn"><p>Fix the highlighted fields first.</p></div>`; return; }
  const run = root.querySelector("#t-run"), cancel = root.querySelector("#t-cancel");
  run.disabled = true; cancel.hidden = false;
  job = runJob("FEA convergence study", "/api/fea/study", { problem, meshes: m });
  try {
    const { id, status } = await job.promise;
    renderStudy(status.result, id, status.exports);
  } catch (e) {
    msg.innerHTML = e instanceof JobError && e.cancelled
      ? `<div class="msg msg-info"><p>Study cancelled; previous plots kept.</p></div>`
      : errorCard(e.info, "FEA study failed");
  } finally { run.disabled = false; cancel.hidden = true; job = null; }
}

function renderStudy(r, jobId, exportsList) {
  const p = r.problem;
  const color = i => PALETTE[i % PALETTE.length];
  // T(x)
  const series = r.profiles.map((pr, i) => ({ label: `FE, N = ${pr.elements}`, x: pr.x, y: pr.T, color: color(i), markers: pr.elements <= 20, markerSize: 2.6 }));
  series.push({ label: "Exact T(x)", x: r.exact.map(e => e[0]), y: r.exact.map(e => e[1]), color: "#111", dashed: true, width: 1.4 });
  linePlot(root.querySelector("#t-plot-T"), { title: "Temperature along the rod", xLabel: "x [m]", yLabel: "T [°C]",
    xName: "x [m]", yName: "T [°C]", series, legend: "bl", filename: "fea_temperature.svg" });
  // error at midpoints
  const es = r.profiles.map((pr, i) => ({ label: `N = ${pr.elements}`, x: pr.midpoints.map(m => m[0]),
    y: pr.midpoints.map(m => m[1] - m[2]), color: color(i), markers: true, markerSize: 2.4 }));
  linePlot(root.querySelector("#t-plot-err"), { title: "FE − exact at element midpoints", xLabel: "x [m]",
    yLabel: "T_h − T_exact [°C]", xName: "x [m]", yName: "error [°C]", series: es, legend: "br", filename: "fea_midpoint_error.svg" });
  // convergence
  const h = r.rows.map(x => x.error.h);
  const mid = r.rows.map(x => x.error.max_midpoint_error), l2 = r.rows.map(x => x.error.l2_error);
  const ref = h.map(v => mid[0] * (v / h[0]) ** 2);
  linePlot(root.querySelector("#t-plot-conv"), { title: "Mesh convergence (log–log)", xLabel: "element size h [m]",
    yLabel: "error [°C]  (L2: °C·m^½)", logX: true, logY: true, xName: "h [m]", yName: "error",
    series: [
      { label: "max |error| at midpoints", x: h, y: mid, color: "#d1495b", markers: true },
      { label: "L2 error norm", x: h, y: l2, color: "#00798c", markers: true },
      { label: "slope 2 reference", x: h, y: ref, color: "#888", dashed: true, noHover: true },
    ], legend: "tl", filename: "fea_convergence.svg" });

  root.querySelector("#t-table").innerHTML = `<table class="data">
    <thead><tr><th>N</th><th>h [m]</th><th>max nodal err [°C]</th><th>max midpoint err [°C]</th><th>125 h² [°C]</th><th>max sampled err [°C]</th><th>L2 err</th><th>rate (mid)</th><th>rate (L2)</th><th>T_max [°C]</th></tr></thead>
    <tbody>${r.rows.map((row, i) => { const e = row.error; return `<tr>
      <td>${e.elements}</td><td>${fmt(e.h, 5)}</td><td>${e.max_nodal_error.toExponential(2)}</td>
      <td>${fmt(e.max_midpoint_error, 7)}</td><td>${fmt(p.heat_gen / (8 * p.conductivity) * e.h * e.h, 7)}</td>
      <td>${fmt(e.max_sampled_error, 7)}</td><td>${fmt(e.l2_error, 6)}</td>
      <td>${i ? row.midpoint_rate.toFixed(4) : "–"}</td><td>${i ? row.l2_rate.toFixed(4) : "–"}</td>
      <td>${fmt(e.max_temperature, 7)}</td></tr>`; }).join("")}</tbody></table>
    <p class="faint" style="margin-top:6px">Midpoint error uses the FE interpolant (T<sub>i</sub>+T<sub>i+1</sub>)/2, not nodal values. The q h²/(8k) column is the analytical prediction for linear elements; "rate" = log(e<sub>coarse</sub>/e<sub>fine</sub>)/log(h ratio).</p>`;
  const last = r.profiles[r.profiles.length - 1];
  const gen = p.heat_gen * p.area * p.length;
  root.querySelector("#t-balance").innerHTML = `<div class="msg msg-info"><p><strong>Energy balance, N = ${last.elements}:</strong>
    heat generated q·A·L = ${fmt(gen, 6)} W; boundary reactions R(0) = ${fmt(last.reaction_left_W, 6)} W, R(L) = ${fmt(last.reaction_right_W, 6)} W;
    residual R(0)+R(L)+qAL = ${last.heat_balance_residual_W.toExponential(2)} W.</p></div>`;
  root.querySelector("#t-exports").innerHTML = exportLinks(jobId, exportsList);

  const e10 = r.rows[0].error, eN = r.rows[r.rows.length - 1];
  root.querySelector("#t-kpis").innerHTML = [
    ["T_max (finest mesh)", fmt(eN.error.max_temperature, 6), "°C"],
    [`midpoint error, N=${e10.elements}`, fmt(e10.max_midpoint_error, 5), "°C"],
    [`midpoint error, N=${eN.error.elements}`, fmt(eN.error.max_midpoint_error, 5), "°C"],
    ["observed rate (last pair)", r.rows.length > 1 ? eN.midpoint_rate.toFixed(4) : "–", "expected 2"],
    ["max nodal error (all meshes)", Math.max(...r.rows.map(x => x.error.max_nodal_error)).toExponential(2), "°C"],
  ].map(([k, v, u]) => `<div class="kpi"><div class="k">${k}</div><div class="v">${v}</div><div class="u">${u}</div></div>`).join("");
}

async function solveSingle() {
  const problem = validate();
  const nRaw = root.querySelector("#t-n").value, n = Number(nRaw);
  const nErr = Number.isInteger(n) && n >= 1 && n <= 1e6 ? "" : "Number of elements must be an integer between 1 and 1,000,000.";
  showMsg("t-n", nErr);
  if (!problem || nErr) return;
  const out = root.querySelector("#t-single-out");
  try {
    const r = await api.post("/api/fea/solve", { problem, elements: n });
    out.hidden = false;
    const show = r.x.length <= 41;
    let asm = "";
    if (r.assembly) {
      const a = r.assembly, N = a.diag.length;
      const rows = [];
      for (let i = 0; i < N; i++) {
        const cells = [];
        for (let j = 0; j < N; j++) {
          const v = j === i ? a.diag[i] : j === i + 1 ? a.sup[i] : j === i - 1 ? a.sub[i] : 0;
          cells.push(`<td class="${v ? "nz" : ""}">${v ? fmt(v, 4) : "·"}</td>`);
        }
        rows.push(`<tr>${cells.join("")}<td class="f">${fmt(a.F[i], 4)}</td></tr>`);
      }
      asm = `<div class="section-label">Assembled global system before boundary conditions: K [W/K] | F [W]</div>
        <div class="table-wrap"><table class="matrix">${rows.join("")}</table></div>
        <p class="faint">Row/column 0 and ${N - 1} are then replaced by the prescribed temperatures (symmetric elimination moves their K-column contributions to F).</p>`;
    }
    out.innerHTML = `<div class="card-head"><h3>Single solve, N = ${n}</h3><span class="spacer"></span>
      <span class="badge badge-info">midpoint err ${fmt(r.error.max_midpoint_error, 5)} °C</span>
      <span class="badge badge-neutral">nodal err ${r.error.max_nodal_error.toExponential(2)} °C</span></div>
      ${show ? `<div class="table-wrap"><table class="data"><thead><tr><th>node</th>${r.x.map((_, i) => `<th>${i}</th>`).join("")}</tr></thead>
      <tbody><tr><td class="text">x [m]</td>${r.x.map(v => `<td>${fmt(v, 4)}</td>`).join("")}</tr>
      <tr><td class="text">T [°C]</td>${r.T.map(v => `<td>${fmt(v, 6)}</td>`).join("")}</tr></tbody></table></div>`
      : `<p class="muted">${r.x.length} nodes; T(max) = ${fmt(r.error.max_temperature, 7)} °C.</p>`}
      ${asm}`;
  } catch (e) {
    out.hidden = false;
    out.innerHTML = errorCard(e.info, "Solve rejected");
  }
}
