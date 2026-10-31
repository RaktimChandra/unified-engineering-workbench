// Task D: serial vs bounded worker-pool batch solves, timing and agreement.
import { api, runJob, JobError, errorCard, exportLinks, esc, fmt, fmtBytes } from "../api.js";
import { linePlot } from "../plot.js";

let root, job = null, sys = null;

export async function mount(el) {
  root = el;
  el.innerHTML = `
  <div class="view-head">
    <h1>Performance: multicore batch solves</h1>
    <span class="muted">A reproducible batch of independent rod solves (fixed seed, varied k and q) is run serially and on a bounded std::thread worker pool. Only the solve loop is timed.</span>
  </div>
  <div class="grid cols-sidebar">
    <div class="stack">
      <div class="card">
        <h3>Workload</h3>
        <form id="p-form" novalidate>
          <div class="field"><label for="p-cases">Cases</label><input type="number" id="p-cases" value="2000" min="1" step="1"><span class="unit">solves</span></div>
          <div class="field"><label for="p-elements">Mesh size</label><input type="number" id="p-elements" value="2000" min="1" step="1"><span class="unit">elements</span></div>
          <div class="field"><label for="p-seed">Seed</label><input type="number" id="p-seed" value="20261101" min="0" step="1"><span class="unit">mt19937_64</span></div>
          <div class="field"><label for="p-warm">Warm-up runs</label><input type="number" id="p-warm" value="1" min="0" max="10" step="1"><span class="unit">discarded</span></div>
          <div class="field"><label for="p-runs">Measured runs</label><input type="number" id="p-runs" value="5" min="1" max="50" step="1"><span class="unit">median</span></div>
          <div class="section-label">Worker counts</div>
          <div class="checks" id="p-workers">${[1, 2, 4, 8].map(n => `<label><input type="checkbox" value="${n}" ${n <= 4 ? "checked" : ""}> ${n}</label>`).join("")}</div>
          <p class="faint" style="margin-top:4px">Counts above the logical CPU count are skipped and reported.</p>
          <div id="p-form-msg"></div>
          <div class="row" style="margin-top:8px">
            <button class="btn btn-primary" type="submit" id="p-run">Run benchmark</button>
            <button class="btn" type="button" id="p-cancel" hidden>Cancel</button>
          </div>
          <div class="progress big" id="p-prog" hidden style="margin-top:8px"><div class="progress-fill" id="p-fill"></div></div>
          <p class="faint" id="p-stage"></p>
        </form>
        <div id="p-msg"></div>
      </div>
      <div class="card" id="p-host"><p class="muted">Loading host information…</p></div>
    </div>
    <div class="stack">
      <div class="kpis" id="p-kpis"></div>
      <div class="grid cols-2">
        <div class="card"><div id="p-plot-speed"></div></div>
        <div class="card"><div id="p-plot-runs"></div></div>
      </div>
      <div class="card">
        <div class="card-head"><h3>Results</h3><span class="spacer"></span><span id="p-agree"></span><span id="p-exports"></span></div>
        <div class="table-wrap" id="p-table"><p class="muted">Run the benchmark to measure this machine.</p></div>
        <div id="p-note"></div>
      </div>
      <div class="card">
        <h3>Method and limitations</h3>
        <ul class="plain">
          <li>Serial reference is a plain loop with no threads; the pool variant creates its threads once, outside the timed region, and hands out chunks of cases through an atomic counter (dynamic scheduling).</li>
          <li>Each case writes only its own slice of a preallocated result array, so output order equals input order and no locks are taken while solving. Each worker owns its scratch buffers, avoiding allocator contention.</li>
          <li>Every parallel result is compared with the serial one over all nodes of all cases; the tolerance is 1e-8 °C. Identical arithmetic in both paths makes the difference exactly zero.</li>
          <li>Speedup = T<sub>serial</sub>/T<sub>p</sub>, efficiency = speedup/p. Each solve is O(N) and memory-bound (Thomas algorithm), so speedup is limited by memory bandwidth, chunk scheduling overhead, turbo-frequency drop when more cores are active, and by SMT siblings sharing one physical core.</li>
          <li>Case generation, thread creation, comparison, file I/O and rendering are excluded from the timings; launch-to-ready time is reported separately.</li>
        </ul>
      </div>
    </div>
  </div>`;
  el.querySelector("#p-form").addEventListener("submit", e => { e.preventDefault(); run(); });
  el.querySelector("#p-cancel").addEventListener("click", () => job?.cancel());
  loadHost();
}

async function loadHost() {
  try { sys = await api.get("/api/system"); renderHost(sys.host, sys.launch_to_ready_ms); }
  catch (e) { root.querySelector("#p-host").innerHTML = errorCard(e.info, "Host information unavailable"); }
}

function renderHost(h, ready) {
  root.querySelector("#p-host").innerHTML = `<h3>This machine</h3>
    <table class="kv">
      <tr><th>CPU</th><td>${esc(h.cpu_model)}</td></tr>
      <tr><th>Logical CPUs</th><td>${h.logical_cpus}</td></tr>
      <tr><th>Physical cores</th><td>${h.physical_cores || "not exposed by OS"}</td></tr>
      <tr><th>RAM</th><td>${fmtBytes(h.ram_bytes)}</td></tr>
      <tr><th>OS</th><td>${esc(h.os)}</td></tr>
      <tr><th>Compiler</th><td>${esc(h.compiler)}</td></tr>
      <tr><th>Build</th><td>${esc(h.build_type)} · <span title="optimisation flags">${esc(h.build_flags || "(none)")}</span></td></tr>
      ${ready !== undefined ? `<tr><th>Launch → ready</th><td>${fmt(ready, 4)} ms <span class="faint">(process start to HTTP listening)</span></td></tr>` : ""}
    </table>
    ${h.under_wine ? `<div class="msg msg-warn"><p>Running under Wine: timings are indicative only.</p></div>` : ""}
    ${h.logical_cpus === 1 ? `<div class="msg msg-warn"><p>Single logical CPU: the parallel path runs and is verified, but speedup cannot be assessed.</p></div>` : ""}`;
}

function intField(id, lo, hi, name) {
  const v = Number(root.querySelector(`#${id}`).value);
  if (!Number.isInteger(v) || v < lo || v > hi) return [null, `${name} must be an integer between ${lo} and ${hi.toLocaleString("en-US")}.`];
  return [v, ""];
}

async function run() {
  const errs = [];
  const [cases, e1] = intField("p-cases", 1, 200000, "Case count"); if (e1) errs.push(e1);
  const [elements, e2] = intField("p-elements", 1, 100000, "Mesh size"); if (e2) errs.push(e2);
  const [seed, e3] = intField("p-seed", 0, Number.MAX_SAFE_INTEGER, "Seed"); if (e3) errs.push(e3);
  const [warm, e4] = intField("p-warm", 0, 10, "Warm-up runs"); if (e4) errs.push(e4);
  const [runs, e5] = intField("p-runs", 1, 50, "Measured runs"); if (e5) errs.push(e5);
  const workers = [...root.querySelectorAll("#p-workers input:checked")].map(c => Number(c.value));
  if (!workers.length) errs.push("Select at least one worker count.");
  if (!errs.length && cases * (elements + 1) > 60e6) errs.push("cases × nodes exceeds 60 million values; reduce the case count or mesh size.");
  const fm = root.querySelector("#p-form-msg");
  fm.innerHTML = errs.length ? `<div class="msg msg-warn"><ul class="plain">${errs.map(e => `<li>${esc(e)}</li>`).join("")}</ul></div>` : "";
  if (errs.length) return;

  const btn = root.querySelector("#p-run"), cancel = root.querySelector("#p-cancel");
  const prog = root.querySelector("#p-prog"), fill = root.querySelector("#p-fill"), stage = root.querySelector("#p-stage");
  btn.disabled = true; cancel.hidden = false; prog.hidden = false; fill.style.width = "0%";
  root.querySelector("#p-msg").innerHTML = "";
  job = runJob("Benchmark", "/api/bench/run", { cases, elements, seed, warmup_runs: warm, measured_runs: runs, workers }, st => {
    fill.style.width = `${Math.round((st.progress || 0) * 100)}%`;
    stage.textContent = st.stage || "";
  });
  try {
    const { id, status } = await job.promise;
    render(status.result, id, status.exports);
    stage.textContent = `Finished in ${fmt(status.elapsed_ms / 1000, 3)} s wall time (including warm-up and checks).`;
  } catch (e) {
    root.querySelector("#p-msg").innerHTML = e instanceof JobError && e.cancelled
      ? `<div class="msg msg-info"><p>Benchmark cancelled; no partial timings are reported.</p></div>`
      : errorCard(e.info, "Benchmark failed");
    stage.textContent = "";
  } finally { btn.disabled = false; cancel.hidden = true; prog.hidden = true; job = null; }
}

function render(r, jobId, exportsList) {
  renderHost(r.host, sys?.launch_to_ready_ms);
  const pool = r.rows.filter(x => x.variant === "pool");
  const serial = r.rows.find(x => x.variant === "serial");
  const best = pool.reduce((a, b) => (b.workers > (a?.workers || 0) ? b : a), null); // highest worker count
  root.querySelector("#p-kpis").innerHTML = [
    ["serial median", fmt(serial.median_s * 1e3, 5), "ms"],
    [best ? `speedup at p=${best.workers}` : "speedup", best ? fmt(best.speedup, 4) : "–", "×"],
    [best ? `efficiency (p=${best.workers})` : "efficiency", best ? `${(best.efficiency * 100).toFixed(1)}` : "–", "%"],
    ["max |serial − parallel|", Math.max(0, ...pool.map(x => x.max_abs_diff_c)).toExponential(1), `°C (limit ${r.tolerance_c})`],
    ["values compared per run", (r.config.cases * (r.config.elements + 1)).toLocaleString("en-US"), "nodal temperatures"],
  ].map(([k, v, u]) => `<div class="kpi"><div class="k">${k}</div><div class="v">${v}</div><div class="u">${u}</div></div>`).join("");

  linePlot(root.querySelector("#p-plot-speed"), {
    title: "Speedup vs workers", xLabel: "workers p [-]", yLabel: "speedup T_serial/T_p [-]", xName: "p", yName: "speedup",
    xTickFmt: v => (Number.isInteger(v) ? String(v) : ""), yMin: 0,
    series: [
      { label: `measured (median of ${r.config.measured_runs})`, x: pool.map(x => x.workers), y: pool.map(x => x.speedup), color: "#30638e", markers: true },
      { label: "ideal (linear)", x: pool.map(x => x.workers), y: pool.map(x => x.workers), color: "#999", dashed: true, noHover: true },
    ], legend: "tl", filename: "benchmark_speedup.svg" });

  const runSeries = r.rows.map((row, i) => ({
    label: row.variant === "serial" ? "serial" : `pool p=${row.workers}`,
    x: row.seconds.map((_, k) => k + 1), y: row.seconds.map(s => s * 1e3),
    color: ["#111", "#d1495b", "#e09f3e", "#00798c", "#30638e", "#6a4c93"][i % 6], markers: true }));
  linePlot(root.querySelector("#p-plot-runs"), {
    title: "Every measured repetition", xLabel: "repetition [-]", yLabel: "wall time [ms]", xName: "run", yName: "ms",
    xTickFmt: v => (Number.isInteger(v) ? String(v) : ""), yMin: 0, series: runSeries, legend: "br", filename: "benchmark_runs.svg" });

  root.querySelector("#p-table").innerHTML = `<table class="data">
    <thead><tr><th>variant</th><th>workers</th><th>median [ms]</th><th>speedup</th><th>efficiency</th><th>max |Δ| [°C]</th>${serial.seconds.map((_, i) => `<th>run ${i + 1} [ms]</th>`).join("")}</tr></thead>
    <tbody>${r.rows.map(row => `<tr><td class="text">${row.variant === "serial" ? "serial loop" : "worker pool"}</td><td>${row.workers}</td>
      <td>${fmt(row.median_s * 1e3, 5)}</td><td>${row.speedup.toFixed(3)}</td><td>${(row.efficiency * 100).toFixed(1)} %</td>
      <td>${row.variant === "serial" ? "ref" : row.max_abs_diff_c.toExponential(1)}</td>
      ${row.seconds.map(s => `<td>${fmt(s * 1e3, 5)}</td>`).join("")}</tr>`).join("")}</tbody></table>
    <p class="faint" style="margin-top:6px">Workload: ${r.config.cases.toLocaleString("en-US")} cases × ${r.config.elements.toLocaleString("en-US")} elements, seed ${r.config.seed},
      k ∈ [${r.config.k_range.join(", ")}] W/(m·K), q ∈ [${r.config.q_range.map(v => fmt(v)).join(", ")}] W/m³, ${r.config.warmup_runs} warm-up + ${r.config.measured_runs} measured runs.
      Case generation ${fmt(r.generate_s * 1e3, 4)} ms (excluded).</p>`;
  root.querySelector("#p-agree").innerHTML = r.agreement_ok
    ? `<span class="badge badge-ok">agreement PASS ≤ ${r.tolerance_c} °C</span>` : `<span class="badge badge-err">agreement FAIL</span>`;
  root.querySelector("#p-exports").innerHTML = exportLinks(jobId, exportsList);
  const notes = [];
  if (r.skipped_workers?.length) notes.push(`Skipped worker counts ${r.skipped_workers.join(", ")}: this machine has ${r.host.logical_cpus} logical CPUs.`);
  if (r.note) notes.push(r.note);
  root.querySelector("#p-note").innerHTML = notes.length ? `<div class="msg msg-warn">${notes.map(n => `<p>${esc(n)}</p>`).join("")}</div>` : "";
}
