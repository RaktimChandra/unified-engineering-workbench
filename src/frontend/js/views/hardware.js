// Task E: simulated microcontroller, FPGA and JTAG adapters driven through
// the C++ mock adapter layer. Everything on this page is SIMULATED.
import { api, runJob, JobError, errorCard, esc, fmt } from "../api.js";
import { sparkline } from "../plot.js";

let root, devices = [];
const state = {};   // id -> { history: {field: []}, last, timeline: [], timer }

export async function mount(el) {
  root = el;
  el.innerHTML = `
  <div class="view-head">
    <h1>Hardware integration bench</h1>
    <span class="badge badge-sim">ALL DEVICES SIMULATED</span>
    <span class="muted">C++ mock adapters implement discovery, capability reporting, connect/disconnect, framed telemetry and fault recovery. No USB device is opened.</span>
  </div>
  <div class="row" style="margin-bottom:10px">
    <button class="btn" id="h-discover">Rescan bus</button>
    <span class="faint" id="h-scan"></span>
  </div>
  <div class="grid cols-3" id="h-cards"></div>
  <div class="grid cols-2" style="margin-top:12px">
    <div class="card diagram">
      <h3>Block diagram</h3>
      <img src="assets/block_diagram.svg" alt="Block diagram: host PC connected over USB to the microcontroller board, FPGA board and JTAG probe, with SPI link MCU to FPGA and JTAG/SWD to the MCU debug port" onerror="this.closest('.card').hidden=true">
    </div>
    <div class="card">
      <h3>Why a JTAG adapter is not a compute board</h3>
      <p>A JTAG/SWD probe bridges the host to a target's Test Access Port (IEEE 1149.1) or Arm debug port. It shifts bits on TCK/TMS/TDI/TDO to read IDCODEs, scan boundary cells, halt and step a core, or program flash. It has no user-programmable compute and no application data path, and it must sense the target's VTref before it drives any pin.</p>
      <p>The three devices therefore expose different capability sets and cannot be swapped for each other. They also differ in connectors (USB Micro-B, USB Micro-B, 20-pin 0.1" / 10-pin 0.05" Cortex debug), voltage domains and host protocols. The adapter interface models these differences instead of hiding them.</p>
      <div class="section-label">Recovery demonstration</div>
      <ol class="plain">
        <li>Press <em>Inject timeout ×1</em> on any device, then <em>Connect</em>: attempt 1 times out, the manager backs off 100 ms, attempt 2 succeeds and the card shows <em>recovered</em>.</li>
        <li><em>Inject timeout ×3</em> exhausts all 3 attempts: the manager reports the failure and resets the device to <em>disconnected</em> so it can be retried cleanly.</li>
        <li><em>Unplug cable</em> drops the link on the next telemetry poll; the device enters <em>fault</em> and a reconnect restores it.</li>
      </ol>
    </div>
  </div>`;
  el.querySelector("#h-discover").addEventListener("click", discover);
  await discover();
}

export function onHide() { /* keep polling connected devices; it is cheap */ }

async function discover() {
  try {
    const r = await api.get("/api/hw/devices");
    devices = r.devices;
    root.querySelector("#h-scan").textContent = `${devices.length} device(s) enumerated on the simulated bus at ${new Date().toLocaleTimeString()}`;
    renderCards();
  } catch (e) {
    root.querySelector("#h-cards").innerHTML = errorCard(e.info, "Device discovery failed");
  }
}

function kindLabel(k) { return { microcontroller: "Microcontroller board", fpga: "FPGA board", jtag_probe: "JTAG / SWD debug probe" }[k] || k; }

function renderCards() {
  const wrap = root.querySelector("#h-cards");
  wrap.innerHTML = devices.map(d => {
    const i = d.info, c = d.capabilities;
    return `<div class="card device" id="dev-${i.id}">
      <div class="ribbon">SIMULATED</div>
      <div class="faint" style="font-size:11px;text-transform:uppercase;letter-spacing:.5px">${kindLabel(i.kind)}</div>
      <h3>${esc(i.model)}</h3>
      <div class="row"><span class="state-pill state-${d.state}" data-role="pill">${d.state}</span>
        <span class="faint mono" style="font-size:11px">id ${esc(i.id)} · S/N ${esc(i.serial)}</span></div>
      <table class="kv">
        <tr><th>USB VID:PID</th><td>${esc(i.usb_vid_pid)}</td></tr>
        <tr><th>Host link</th><td>${esc(i.host_link)}</td></tr>
        <tr><th>Target I/O</th><td>${esc(i.io_voltage)}</td></tr>
      </table>
      ${c.general_compute ? "" : `<div class="no-compute">Debug interface: no general compute. It cannot run workloads or stand in for a board.</div>`}
      <details ${c.general_compute ? "" : "open"}><summary>Capabilities (${c.items.length})</summary>
        <ul class="caps">${c.items.map(x => `<li><code>${esc(x.name)}</code> <span class="muted">${esc(x.detail)}</span></li>`).join("")}</ul></details>
      <div class="row">
        <button class="btn btn-primary" data-act="connect">Connect</button>
        <button class="btn" data-act="disconnect">Disconnect</button>
      </div>
      <div class="row">
        <span class="faint" style="font-size:11px">Fault injection:</span>
        <button class="btn btn-small btn-danger" data-act="t1" title="Next connect attempt times out">Inject timeout ×1</button>
        <button class="btn btn-small btn-danger" data-act="t3" title="Next 3 connect attempts time out">×3</button>
        <button class="btn btn-small btn-danger" data-act="drop" title="Drop the USB link on the next telemetry poll">Unplug cable</button>
      </div>
      <div data-role="timeline"></div>
      <div data-role="telemetry"><p class="faint">Connect to stream telemetry (1 Hz).</p></div>
    </div>`;
  }).join("");
  for (const d of devices) {
    const card = root.querySelector(`#dev-${d.info.id}`);
    state[d.info.id] ??= { history: {}, timeline: [] };
    card.addEventListener("click", e => {
      const act = e.target.closest("[data-act]")?.dataset.act;
      if (act) action(d.info.id, act);
    });
    applyState(d.info.id, d.state);
    renderTimeline(d.info.id);
    if (d.state === "connected") startPolling(d.info.id);
  }
}

function card(id) { return root.querySelector(`#dev-${id}`); }

function applyState(id, s) {
  const c = card(id); if (!c) return;
  const pill = c.querySelector("[data-role=pill]");
  pill.className = `state-pill state-${s}`; pill.textContent = s;
  c.querySelector("[data-act=connect]").disabled = s === "connected" || s === "connecting";
  c.querySelector("[data-act=disconnect]").disabled = s === "disconnected";
}

function log(id, cls, text) {
  const st = state[id];
  st.timeline.push({ cls, text, t: new Date().toLocaleTimeString() });
  if (st.timeline.length > 8) st.timeline.shift();
  renderTimeline(id);
}

function renderTimeline(id) {
  const c = card(id); if (!c) return;
  const tl = state[id].timeline;
  c.querySelector("[data-role=timeline]").innerHTML = tl.length ? `<div class="section-label">Event timeline</div><ul class="timeline">${tl.map((e, i) =>
    `<li><span class="tl-n">${i + 1}</span><span class="badge ${e.cls}">${e.t}</span><span>${esc(e.text)}</span></li>`).join("")}</ul>` : "";
}

async function action(id, act) {
  try {
    if (act === "t1" || act === "t3") {
      const n = act === "t1" ? 1 : 3;
      await api.post(`/api/hw/${id}/fault`, { connect_timeouts: n, drop_link: false });
      log(id, "badge-warn", `Armed: next ${n} connect attempt(s) will time out`);
    } else if (act === "drop") {
      await api.post(`/api/hw/${id}/fault`, { connect_timeouts: 0, drop_link: true });
      log(id, "badge-warn", "Armed: link drops on next telemetry poll");
    } else if (act === "disconnect") {
      stopPolling(id);
      await api.post(`/api/hw/${id}/disconnect`);
      applyState(id, "disconnected");
      log(id, "badge-neutral", "Disconnected by user");
    } else if (act === "connect") {
      applyState(id, "connecting");
      log(id, "badge-info", "Connecting (timeout 250 ms per attempt, up to 3 attempts)");
      const job = runJob(`Connect ${id}`, `/api/hw/${id}/connect`);
      const { status } = await job.promise;
      const o = status.result;
      for (const a of o.attempts) {
        if (a.status.ok) log(id, "badge-ok", `Attempt ${a.attempt}: connected`);
        else log(id, "badge-err", `Attempt ${a.attempt}: ${a.status.code}${a.backoff_ms ? ` → back off ${a.backoff_ms} ms` : ""}`);
      }
      if (o.status.ok) {
        if (o.recovered) log(id, "badge-ok", `Recovered after ${o.attempts.length} attempts`);
        applyState(id, "connected");
        startPolling(id);
      } else {
        log(id, "badge-err", o.status.message);
        applyState(id, "disconnected");
      }
    }
  } catch (e) {
    const info = e instanceof JobError ? e.info : e.info;
    log(id, "badge-err", info?.message || "request failed");
    refreshState(id);
  }
}

async function refreshState(id) {
  try {
    const r = await api.get("/api/hw/devices");
    const d = r.devices.find(x => x.info.id === id);
    if (d) applyState(id, d.state);
  } catch {}
}

function startPolling(id) {
  stopPolling(id);
  const st = state[id];
  const tick = async () => {
    try {
      const { status, data } = await api.get(`/api/hw/${id}/telemetry`, { withStatus: true, allowStatus: [409] });
      if (status === 409) {
        stopPolling(id);
        applyState(id, data.state);
        log(id, "badge-err", data.status.message + (data.status.retryable ? " Press Connect to recover." : ""));
        return;
      }
      renderTelemetry(id, data.message);
    } catch { /* backend banner handles it */ }
  };
  tick();
  st.timer = setInterval(tick, 1000);
}

function stopPolling(id) { const st = state[id]; if (st?.timer) { clearInterval(st.timer); st.timer = null; } }

function renderTelemetry(id, m) {
  const st = state[id];
  for (const [k, f] of Object.entries(m.fields)) {
    (st.history[k] ??= []).push(f.value);
    if (st.history[k].length > 30) st.history[k].shift();
  }
  const rows = Object.entries(m.fields).map(([k, f]) => {
    const val = f.unit === "hex" ? `0x${(f.value >>> 0).toString(16).toUpperCase().padStart(8, "0")}` : `${fmt(f.value, 5)} ${esc(f.unit)}`;
    return `<span>${esc(k)}</span><span class="tv">${val}</span>${f.unit === "hex" || f.unit === "kHz" ? "<span></span>" : sparkline(st.history[k])}`;
  }).join("");
  card(id).querySelector("[data-role=telemetry]").innerHTML = `
    <div class="section-label">Telemetry message</div>
    <div class="row" style="margin-bottom:4px">
      <span class="badge badge-sim">simulated</span>
      <span class="mono faint" style="font-size:11px">seq ${m.sequence} · t ${(m.device_time_ms / 1000).toFixed(0)} s · CRC32 ${esc(m.crc32)}</span>
      ${m.crc_ok ? `<span class="badge badge-ok">CRC OK</span>` : `<span class="badge badge-err">CRC MISMATCH</span>`}
    </div>
    <div class="telemetry">${rows}</div>`;
}
