// Thin HTTP client for the C++ backend plus job polling with progress/cancel.
// Every long computation is a backend job; the UI only polls, never blocks.

const listeners = new Set();
let backendUp = true;

export function onBackendState(fn) { listeners.add(fn); }
function setBackend(up) {
  if (up === backendUp) return;
  backendUp = up;
  for (const fn of listeners) fn(up);
}

export class ApiError extends Error {
  constructor(info, status) {
    super(info?.message || `HTTP ${status}`);
    this.info = info || { message: `HTTP ${status}` };
    this.status = status;
  }
}

async function request(method, url, body, opts = {}) {
  let res;
  try {
    const init = { method, headers: {} };
    if (body instanceof ArrayBuffer || body instanceof Uint8Array || body instanceof Blob) {
      init.body = body;
      init.headers["Content-Type"] = "application/octet-stream";
    } else if (body !== undefined) {
      init.body = JSON.stringify(body);
      init.headers["Content-Type"] = "application/json";
    }
    res = await fetch(url, init);
  } catch (e) {
    setBackend(false);
    throw new ApiError({ code: "backend_unreachable", message: "The local backend is not reachable.",
      hint: "Make sure Launch.exe is still running, then retry." }, 0);
  }
  setBackend(true);
  if (opts.raw) {
    if (!res.ok) throw new ApiError({ message: `HTTP ${res.status}` }, res.status);
    return res;
  }
  let data = null;
  const text = await res.text();
  try { data = text ? JSON.parse(text) : null; } catch { data = { error: { message: text } }; }
  if (!res.ok && !opts.allowStatus?.includes(res.status)) {
    throw new ApiError(data?.error || { message: `HTTP ${res.status}` }, res.status);
  }
  return opts.withStatus ? { status: res.status, data } : data;
}

export const api = {
  get: (url, opts) => request("GET", url, undefined, opts),
  post: (url, body, opts) => request("POST", url, body, opts),
  async blob(jobId) {
    const res = await request("GET", `/api/jobs/${jobId}/blob`, undefined, { raw: true });
    return res.arrayBuffer();
  },
  exportUrl: (jobId, name) => `/api/jobs/${jobId}/export/${encodeURIComponent(name)}`,
};

// ---- jobs -------------------------------------------------------------------
// Global "current job" tracker drives the status bar.
const jobListeners = new Set();
export function onJobUpdate(fn) { jobListeners.add(fn); }
function emitJob(ev) { for (const fn of jobListeners) fn(ev); }

/** Thrown when a job ends in failed/cancelled state. */
export class JobError extends Error {
  constructor(status) {
    super(status.state === "cancelled" ? "Cancelled" : (status.error?.message || "Job failed"));
    this.status = status;
    this.cancelled = status.state === "cancelled";
    this.info = status.error || { message: this.message };
  }
}

/**
 * Starts a job via POST and polls it until it finishes.
 * onProgress(status) is called on every poll. Returns {id, status}.
 * The returned promise has a .cancel() helper via the `handle` object.
 */
export function runJob(label, url, body, onProgress) {
  const handle = { id: null, cancelled: false, cancel: async () => {
    handle.cancelled = true;
    if (handle.id) { try { await api.post(`/api/jobs/${handle.id}/cancel`); } catch {} }
  } };
  handle.promise = (async () => {
    const start = await api.post(url, body);
    handle.id = start.job_id;
    if (handle.cancelled) await handle.cancel();
    emitJob({ label, handle, status: { state: "queued", progress: 0, stage: "queued" } });
    let delay = 120;
    for (;;) {
      const st = await api.get(`/api/jobs/${handle.id}`);
      onProgress?.(st);
      emitJob({ label, handle, status: st });
      if (st.state === "done") return { id: handle.id, status: st };
      if (st.state === "failed" || st.state === "cancelled") throw new JobError(st);
      await new Promise(r => setTimeout(r, delay));
      delay = Math.min(400, delay + 20);
    }
  })();
  return handle;
}

// ---- formatting helpers ----------------------------------------------------
export function fmt(v, digits = 6) {
  if (v === null || v === undefined || Number.isNaN(v)) return "–";
  if (typeof v !== "number") return String(v);
  if (v === 0) return "0";
  const a = Math.abs(v);
  if (a >= 1e6 || a < 1e-4) return v.toExponential(Math.max(1, digits - 3)).replace("e+", "e");
  return Number(v.toPrecision(digits)).toString();
}
export function fmtDuration(ms) {
  if (ms === undefined || ms === null) return "–";
  if (ms < 1) return "<1 ms";
  if (ms < 1000) return `${ms.toFixed(ms < 10 ? 1 : 0)} ms`;
  return `${(ms / 1000).toFixed(2)} s`;
}
export function fmtBytes(b) {
  if (!b && b !== 0) return "–";
  const u = ["B", "KiB", "MiB", "GiB", "TiB"]; let i = 0; let v = b;
  while (v >= 1024 && i < u.length - 1) { v /= 1024; i++; }
  return `${v.toFixed(i ? 2 : 0)} ${u[i]}`;
}
export function esc(s) {
  return String(s ?? "").replace(/[&<>"']/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
}

/** Renders an actionable error card for {code,message,hint,line,file}. */
export function errorCard(info, title = "Could not complete the request") {
  const line = info?.line ? ` <span class="line-ref">line ${esc(info.line)}</span>` : "";
  const file = info?.file ? ` <span class="faint">(${esc(info.file)})</span>` : "";
  const code = info?.code ? ` <span class="badge badge-neutral mono">${esc(info.code)}</span>` : "";
  return `<div class="msg msg-error" role="alert">
    <h3>${esc(title)}${code}</h3>
    <p>${esc(info?.message || "Unknown error")}${line}${file}</p>
    ${info?.hint ? `<p class="hint">${esc(info.hint)}</p>` : ""}
  </div>`;
}

export function exportLinks(jobId, names) {
  if (!names?.length) return "";
  return `<div class="exports">${names.map(n =>
    `<a href="${api.exportUrl(jobId, n)}" download="${esc(n)}" title="Download ${esc(n)} (generated by the C++ backend)">${esc(n)}</a>`).join("")}</div>`;
}

export const h = (html) => { const t = document.createElement("template"); t.innerHTML = html.trim(); return t.content.firstElementChild; };
