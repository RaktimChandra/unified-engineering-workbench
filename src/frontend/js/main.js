// App shell: tab routing, status bar (current job, progress, cancel),
// backend log console, heartbeat/auto-shutdown and the Quit button.
import { api, onBackendState, onJobUpdate, esc, fmtDuration } from "./api.js";

const VIEWS = {
  geometry: () => import("./views/geometry.js"),
  thermal: () => import("./views/thermal.js"),
  performance: () => import("./views/performance.js"),
  hardware: () => import("./views/hardware.js"),
  backprop: () => import("./views/backprop.js"),
  verify: () => import("./views/verify.js"),
};
const mounted = {};
let current = null;

async function show(name) {
  if (!VIEWS[name]) name = "geometry";
  for (const b of document.querySelectorAll(".tabs button")) {
    const on = b.dataset.view === name;
    b.setAttribute("aria-selected", String(on));
    b.tabIndex = on ? 0 : -1;
  }
  for (const v of document.querySelectorAll(".view")) v.hidden = v.id !== `view-${name}`;
  if (current && mounted[current]?.onHide) mounted[current].onHide();
  current = name;
  const el = document.getElementById(`view-${name}`);
  try {
    if (!mounted[name]) {
      const mod = await VIEWS[name]();
      mounted[name] = mod;
      await mod.mount(el);
    }
    mounted[name].onShow?.();
  } catch (e) {
    console.error(e);
    el.innerHTML = `<div class="msg msg-error"><h3>This view failed to load</h3><p>${esc(e.message)}</p></div>`;
  }
}

const tabs = document.querySelector(".tabs");
tabs.addEventListener("click", e => {
  const b = e.target.closest("button[data-view]");
  if (b) location.hash = b.dataset.view;
});
tabs.addEventListener("keydown", e => {
  if (e.key !== "ArrowRight" && e.key !== "ArrowLeft") return;
  const btns = [...tabs.querySelectorAll("button")];
  const i = btns.findIndex(b => b.getAttribute("aria-selected") === "true");
  const n = btns[(i + (e.key === "ArrowRight" ? 1 : btns.length - 1)) % btns.length];
  n.focus(); location.hash = n.dataset.view;
});
window.addEventListener("hashchange", () => show(location.hash.slice(1)));
show(location.hash.slice(1) || "geometry");

// ---- status bar ------------------------------------------------------------
const sDot = document.getElementById("status-dot");
const sText = document.getElementById("status-text");
const sProg = document.getElementById("status-progress");
const sFill = document.getElementById("status-fill");
const sCancel = document.getElementById("status-cancel");
let activeHandle = null;
let clearTimer = null;

onJobUpdate(({ label, handle, status }) => {
  clearTimeout(clearTimer);
  const st = status.state;
  const pct = Math.round((status.progress || 0) * 100);
  const running = st === "queued" || st === "running";
  sDot.className = `status-dot ${running ? "busy" : st === "done" ? "ok" : "err"}`;
  const elapsed = status.elapsed_ms !== undefined ? ` · ${fmtDuration(status.elapsed_ms)}` : "";
  sText.textContent = running ? `${label}: ${status.stage || st} (${pct}%)${elapsed}`
    : st === "done" ? `${label}: done${elapsed}`
    : st === "cancelled" ? `${label}: cancelled; previous results kept`
    : `${label}: failed - ${status.error?.message || ""}`;
  sProg.hidden = !running;
  sFill.style.width = `${pct}%`;
  sCancel.hidden = !running;
  activeHandle = running ? handle : null;
  if (!running) clearTimer = setTimeout(() => { sDot.className = "status-dot idle"; sText.textContent = "Idle"; }, 8000);
});
sCancel.addEventListener("click", () => activeHandle?.cancel());

// ---- backend connectivity ------------------------------------------------
const conn = document.getElementById("conn-state");
const down = document.getElementById("backend-down");
onBackendState(up => {
  conn.className = `conn ${up ? "ok" : "down"}`;
  conn.textContent = up ? "backend online" : "backend offline";
  down.hidden = up;
});

(async () => {
  try {
    const sys = await api.get("/api/system");
    conn.className = "conn ok"; conn.textContent = "backend online";
    document.getElementById("app-version").textContent = `v${sys.version}`;
    const ready = sys.launch_to_ready_ms;
    if (ready) document.getElementById("status-ready").textContent =
      `launch-to-ready ${ready.toFixed(0)} ms · ${sys.host.logical_cpus} logical CPUs`;
  } catch { /* banner shown by onBackendState */ }
})();

// ---- heartbeat & lifecycle ------------------------------------------------
async function beat() { try { await api.post("/api/heartbeat"); } catch {} }
beat();
setInterval(beat, 5000);
window.addEventListener("pagehide", () => { try { navigator.sendBeacon("/api/bye"); } catch {} });

document.getElementById("btn-quit").addEventListener("click", async () => {
  if (!confirmQuit()) return;
  try { await api.post("/api/shutdown"); } catch {}
  document.getElementById("quit-overlay").hidden = false;
});
function confirmQuit() {
  // window.confirm would block automation and screen readers handle it poorly;
  // a second click within 3 s confirms instead.
  const b = document.getElementById("btn-quit");
  if (b.dataset.armed) return true;
  b.dataset.armed = "1"; b.textContent = "Click again to quit";
  setTimeout(() => { delete b.dataset.armed; b.textContent = "Quit"; }, 3000);
  return false;
}

// ---- log console -----------------------------------------------------------
const logEl = document.getElementById("log-console");
const logLines = document.getElementById("log-lines");
const toggle = document.getElementById("toggle-log");
let logSeq = 0;
function setLog(open) { logEl.hidden = !open; toggle.setAttribute("aria-expanded", String(open)); if (open) logLines.scrollTop = 1e9; }
toggle.addEventListener("click", () => setLog(logEl.hidden));
document.getElementById("log-close").addEventListener("click", () => setLog(false));
async function pollLog() {
  try {
    const { lines } = await api.get(`/api/log?since=${logSeq}`);
    if (lines.length) {
      const atBottom = logLines.scrollTop + logLines.clientHeight >= logLines.scrollHeight - 4;
      const frag = lines.map(l => `<span class="lv-t">[${l.t.toFixed(3).padStart(9)}]</span> <span class="lv-${l.level}">${l.level.padEnd(5)} ${esc(l.msg)}</span>`).join("\n");
      logLines.insertAdjacentHTML("beforeend", (logLines.textContent ? "\n" : "") + frag);
      logSeq = lines[lines.length - 1].seq;
      if (atBottom) logLines.scrollTop = logLines.scrollHeight;
    }
  } catch {}
}
pollLog();
setInterval(pollLog, 1000);
