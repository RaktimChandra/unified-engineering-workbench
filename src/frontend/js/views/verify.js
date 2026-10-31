// In-app self-verification: runs the headline numerical checks inside the
// shipped executable, so results can be reproduced on the assessor's machine.
import { api, runJob, errorCard, esc, fmt, fmtBytes } from "../api.js";

let root;

export async function mount(el) {
  root = el;
  el.innerHTML = `
  <div class="view-head">
    <h1>Self-verification</h1>
    <span class="muted">Runs the key numerical and robustness checks of the test suite inside this executable: FEA exactness and convergence, solver cross-check, import validation, parallel agreement, gradient check and fault recovery.</span>
  </div>
  <div class="grid cols-sidebar">
    <div class="stack">
      <div class="card">
        <button class="btn btn-primary" id="v-run">Run self-verification</button>
        <p class="faint" style="margin-top:8px">The full suite (positive, negative, numerical and API tests) is in <code>tests/</code> and runs with <code>ctest</code>.</p>
        <div id="v-summary"></div>
      </div>
      <div class="card" id="v-host"></div>
    </div>
    <div class="card"><ul class="checklist" id="v-list"><li><span></span><span class="muted">Not run yet.</span></li></ul></div>
  </div>`;
  el.querySelector("#v-run").addEventListener("click", run);
  try {
    const s = await api.get("/api/system");
    const h = s.host;
    el.querySelector("#v-host").innerHTML = `<h3>Environment</h3><table class="kv">
      <tr><th>Version</th><td>${esc(s.version)}</td></tr><tr><th>OS</th><td>${esc(h.os)}</td></tr>
      <tr><th>CPU</th><td>${esc(h.cpu_model)}</td></tr><tr><th>RAM</th><td>${fmtBytes(h.ram_bytes)}</td></tr>
      <tr><th>Compiler</th><td>${esc(h.compiler)}</td></tr><tr><th>Launch → ready</th><td>${fmt(s.launch_to_ready_ms, 4)} ms</td></tr>
      <tr><th>Upload limit</th><td>${fmtBytes(s.limits.max_upload_bytes)}</td></tr></table>`;
  } catch {}
}

async function run() {
  const btn = root.querySelector("#v-run");
  btn.disabled = true;
  root.querySelector("#v-list").innerHTML = `<li><span></span><span class="muted">Running…</span></li>`;
  try {
    const { status } = await runJob("Self-verification", "/api/selftest").promise;
    const r = status.result;
    const passed = r.checks.filter(c => c.pass).length;
    root.querySelector("#v-summary").innerHTML = `<div class="msg ${r.all_pass ? "msg-ok" : "msg-error"}" style="margin-top:10px">
      <div class="summary-big">${passed} / ${r.checks.length} checks passed</div>
      <p>Completed in ${fmt(status.elapsed_ms / 1000, 3)} s at ${new Date().toLocaleTimeString()}.</p></div>`;
    root.querySelector("#v-list").innerHTML = r.checks.map(c => `<li>
      <span>${c.pass ? '<span class="badge badge-ok">PASS</span>' : '<span class="badge badge-err">FAIL</span>'}</span>
      <span><div>${esc(c.name)}</div><div class="detail">${esc(c.detail)}</div></span></li>`).join("");
  } catch (e) {
    root.querySelector("#v-list").innerHTML = `<li><span></span><span>${errorCard(e.info, "Self-verification could not run")}</span></li>`;
  } finally { btn.disabled = false; }
}
