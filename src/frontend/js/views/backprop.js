// Task F: hand-derived backpropagation for h = tanh(wx+b), y_hat = vh + c.
import { runJob, errorCard, exportLinks, fmt } from "../api.js";
import { linePlot } from "../plot.js";

let root;

export async function mount(el) {
  root = el;
  el.innerHTML = `
  <div class="view-head">
    <h1>Backpropagation check</h1>
    <span class="muted">One sample x = 1, y = 2; h = tanh(wx + b), ŷ = vh + c, J = ½(ŷ − y)². Gradients are written out by hand in C++ (no automatic differentiation) and checked by central finite differences.</span>
  </div>
  <div class="grid cols-sidebar">
    <div class="stack">
      <div class="card">
        <h3>Run</h3>
        <div class="field"><label for="b-steps">Training steps</label><input type="number" id="b-steps" value="100" min="1" step="1"><span class="unit">steps</span></div>
        <div class="field"><label for="b-lr">Learning rate</label><input type="number" id="b-lr" value="0.1" step="any"><span class="unit">η</span></div>
        <div id="b-form-msg"></div>
        <button class="btn btn-primary" id="b-run">Compute</button>
        <div id="b-msg"></div>
      </div>
      <div class="card">
        <h3>Chain rule</h3>
        <div class="eq">z = w x + b,  h = tanh z,  ŷ = v h + c
e = ŷ − y,  J = ½ e²
∂J/∂c = e
∂J/∂v = e · h
∂J/∂b = e · v · (1 − h²)
∂J/∂w = e · v · (1 − h²) · x
Update (simultaneous, original gradients):
θ ← θ − η ∂J/∂θ</div>
        <p class="faint" style="margin-top:6px">Initial w = 0.5, b = 0, v = 1, c = 0. Finite differences: (J(θ+ε) − J(θ−ε)) / 2ε with ε = 1e-6; pass if |analytic − FD| ≤ 1e-5.</p>
      </div>
    </div>
    <div class="stack">
      <div class="kpis" id="b-kpis"></div>
      <div class="grid cols-2">
        <div class="card"><h3>Gradient check</h3><div class="table-wrap" id="b-grad"></div></div>
        <div class="card"><h3>Forward pass and one update</h3><div class="table-wrap" id="b-update"></div></div>
      </div>
      <div class="card"><div class="card-head"><h3>Loss history</h3><span class="spacer"></span><span id="b-exports"></span></div>
        <div id="b-plot"></div><div id="b-explain"></div></div>
    </div>
  </div>`;
  el.querySelector("#b-run").addEventListener("click", run);
  run();
}

let busy = false;
async function run() {
  if (busy) return; // ignore a second click while the first job is in flight
  const steps = Number(root.querySelector("#b-steps").value), lr = Number(root.querySelector("#b-lr").value);
  const errs = [];
  if (!Number.isInteger(steps) || steps < 1 || steps > 100000) errs.push("steps must be an integer 1..100000.");
  if (!(lr > 0 && lr < 10)) errs.push("learning rate must be in (0, 10).");
  root.querySelector("#b-form-msg").innerHTML = errs.length ? `<div class="msg msg-warn"><p>${errs.join(" ")}</p></div>` : "";
  if (errs.length) return;
  root.querySelector("#b-msg").innerHTML = "";
  try {
    busy = true;
    const btn = root.querySelector("#b-run");
    if (btn) btn.disabled = true;
    const { id, status } = await runJob("Backprop", "/api/backprop/run", { steps, learning_rate: lr }).promise.finally(() => {
      busy = false;
      if (btn) btn.disabled = false;
    });
    render(status.result, id, status.exports);
  } catch (e) { root.querySelector("#b-msg").innerHTML = errorCard(e.info, "Backprop run failed"); }
}

function render(r, id, exportsList) {
  const P = ["w", "b", "v", "c"];
  const g = r.gradients, fd = r.finite_difference;
  const worst = Math.max(...P.map(p => Math.abs(g[p] - fd[p])));
  root.querySelector("#b-grad").innerHTML = `<table class="data"><thead><tr><th>θ</th><th>analytic ∂J/∂θ</th><th>central diff.</th><th>|difference|</th><th></th></tr></thead>
    <tbody>${P.map(p => { const d = Math.abs(g[p] - fd[p]); return `<tr><td>${p}</td><td>${g[p].toFixed(10)}</td><td>${fd[p].toFixed(10)}</td>
      <td>${d.toExponential(2)}</td><td>${d <= 1e-5 ? '<span class="badge badge-ok">pass</span>' : '<span class="badge badge-err">fail</span>'}</td></tr>`; }).join("")}</tbody></table>`;
  const f = r.forward;
  root.querySelector("#b-update").innerHTML = `<table class="data">
    <tbody>
      <tr><td class="text">z = wx + b</td><td>${f.z.toFixed(8)}</td></tr>
      <tr><td class="text">h = tanh(z)</td><td>${f.h.toFixed(8)}</td></tr>
      <tr><td class="text">ŷ = vh + c</td><td>${f.y_hat.toFixed(8)}</td></tr>
      <tr><td class="text">e = ŷ − y</td><td>${f.error.toFixed(8)}</td></tr>
      <tr><td class="text">J = ½e²</td><td>${f.loss.toFixed(8)}</td></tr>
    </tbody></table>
    <table class="data" style="margin-top:8px"><thead><tr><th>θ</th><th>before</th><th>− η·∂J/∂θ</th><th>after</th></tr></thead>
    <tbody>${P.map(p => `<tr><td>${p}</td><td>${r.initial[p].toFixed(8)}</td><td>${(-r.learning_rate * g[p]).toFixed(8)}</td><td>${r.updated[p].toFixed(8)}</td></tr>`).join("")}
      <tr><td class="text">J</td><td>${f.loss.toFixed(8)}</td><td></td><td>${r.updated_loss.toFixed(8)}</td></tr></tbody></table>`;
  const L = r.loss;
  linePlot(root.querySelector("#b-plot"), { title: `Training loss, η = ${r.learning_rate}`, xLabel: "gradient-descent step [-]",
    yLabel: "loss J [-] (log scale)", logY: true, xName: "step", yName: "J", height: 320,
    series: [{ label: "J (step 0 = initial parameters)", x: L.map((_, i) => i), y: L, color: "#6a4c93" }], filename: "backprop_loss.svg" });
  root.querySelector("#b-exports").innerHTML = exportLinks(id, exportsList);
  root.querySelector("#b-kpis").innerHTML = [
    ["initial loss J₀", fmt(L[0], 7), ""], ["after 1 step", fmt(L[1], 7), ""],
    [`after ${L.length - 1} steps`, fmt(L[L.length - 1], 4), ""], ["worst gradient error", worst.toExponential(2), "limit 1e-5"],
  ].map(([k, v, u]) => `<div class="kpi"><div class="k">${k}</div><div class="v">${v}</div><div class="u">${u}</div></div>`).join("");
  root.querySelector("#b-explain").innerHTML = `<div class="note" style="margin-top:8px"><strong>Reading the result.</strong>
    ŷ starts below the target (e = ${f.error.toFixed(4)} &lt; 0), so every gradient is negative and descent increases all four parameters, raising ŷ toward 2.
    c moves most because ∂J/∂c = e directly; w and b are damped by the tanh slope 1 − h² = ${(1 - f.h * f.h).toFixed(4)}.
    With one sample and four parameters the model can fit exactly, so J falls monotonically toward zero; the log-scale curve becomes roughly a straight line, which is the linear convergence expected of fixed-step gradient descent near a minimum.</div>`;
}
