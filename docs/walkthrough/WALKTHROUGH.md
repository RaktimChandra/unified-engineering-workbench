# Demonstration walkthrough (timestamped screenshots, log and video)

**Environment, stated plainly:** this recording shows the packaged Windows `Launch.exe` (MinGW-w64 GCC 13, statically linked, Release `-O3`) running under **Wine 9.0 on a Linux container** (2 logical CPUs, Intel Xeon @ 2.10 GHz, 7.8 GiB RAM), with **Chromium driven by Playwright** as the browser and **SwiftShader software WebGL**. It is the same binary and folder layout as in the ZIP (`Launch.exe`, `app/web/`, `data/`), started with `--no-browser --port 47900` so the automation could attach. A native Windows 11 run is to be recorded by the candidate. Timings measured under Wine (for example the benchmark speedup in step 16) are indicative only. The reference benchmark is in `results/benchmark_host.txt`.

- Video: [`demo_walkthrough.mp4`](demo_walkthrough.mp4), 4 min 2 s, 1366x768 H.264, no audio. A caption box in the lower left describes each action with `[mm:ss]` elapsed time.
- Server log for the same session: [`server_log.txt`](server_log.txt). It is copied from `logs/workbench.log` next to `Launch.exe`; one absolute path is redacted.
- Overview image: [`contact_sheet.jpg`](contact_sheet.jpg).

| # | Wall clock (UTC, ISO 8601) | Elapsed | Action | What to observe | Screenshot |
|---|---|---|---|---|---|
| 1 | 2026-10-31T22:35:26.491Z | 00:06 | Launch.exe started; browser opened the local UI | Status bar shows launch-to-ready ms and "backend online" | [01_app-opened.jpg](01_app-opened.jpg) |
| 2 | 2026-10-31T22:35:35.843Z | 00:16 | Loaded heatsink_fins.stl | 156 triangles, closed 2-manifold, volume 28800 mm^3, bounds in mm | [02_stl-loaded.jpg](02_stl-loaded.jpg) |
| 3 | 2026-10-31T22:35:51.861Z | 00:32 | Rotated, panned and zoomed the mesh | Interactive 3D view responds; UI stays responsive | [03_stl-rotated-zoomed.jpg](03_stl-rotated-zoomed.jpg) |
| 4 | 2026-10-31T22:36:06.224Z | 00:46 | Re-imported with unit = metre | Statistics now labelled in m / m^2 / m^3 | [04_stl-units-metres.jpg](04_stl-units-metres.jpg) |
| 5 | 2026-10-31T22:36:15.411Z | 00:55 | Imported malformed/stl_nan_vertex.stl | Error card: stl.non_finite, line 5, hint; old model still displayed | [05_stl-nan-error.jpg](05_stl-nan-error.jpg) |
| 6 | 2026-10-31T22:36:23.796Z | 01:04 | Imported malformed/stl_binary.stl | Error card: stl.binary with re-export hint | [06_stl-binary-error.jpg](06_stl-binary-error.jpg) |
| 7 | 2026-10-31T22:36:35.503Z | 01:15 | Loaded the SYNTHETIC nodal CSV | 8,274 points coloured by temperature, colour bar in degC, SYNTHETIC badge | [07_csv-point-cloud.jpg](07_csv-point-cloud.jpg) |
| 8 | 2026-10-31T22:36:42.830Z | 01:23 | Clicked a node in the point cloud | Inspector shows node_id, x/y/z in m, temperature in degC | [08_csv-node-inspect.jpg](08_csv-node-inspect.jpg) |
| 9 | 2026-10-31T22:36:51.263Z | 01:31 | Imported malformed/csv_semicolon.csv | Error card: csv.semicolon, line 1, re-export hint | [09_csv-semicolon-error.jpg](09_csv-semicolon-error.jpg) |
| 10 | 2026-10-31T22:37:00.688Z | 01:40 | Ran the convergence study (Task C defaults) | FE vs exact profiles, midpoint error 1.25 -> 0.0195 C, rate 2.0000, nodal error ~1e-12 | [10_fea-study.jpg](10_fea-study.jpg) |
| 11 | 2026-10-31T22:37:06.275Z | 01:46 | Scrolled to the log-log plot and table | Midpoint error equals 125 h^2 exactly; energy balance residual ~1e-15 W | [11_fea-convergence-table.jpg](11_fea-convergence-table.jpg) |
| 12 | 2026-10-31T22:37:15.385Z | 01:55 | Single solve with N = 4 | Assembled K (kA/h entries) and F (qAh/2 entries) | [12_fea-assembly.jpg](12_fea-assembly.jpg) |
| 13 | 2026-10-31T22:37:21.943Z | 02:02 | Entered k = -1 and pressed Run | Inline message: k must be finite and > 0 W/(m K) | [13_fea-invalid-k.jpg](13_fea-invalid-k.jpg) |
| 14 | 2026-10-31T22:37:30.247Z | 02:10 | Clicked fea_convergence.csv | Download received: fea_convergence.csv | [14_fea-export.jpg](14_fea-export.jpg) |
| 15 | 2026-10-31T22:37:39.784Z | 02:20 | Started the benchmark | Progress bar and stage text update; UI stays responsive | [15_bench-running.jpg](15_bench-running.jpg) |
| 16 | 2026-10-31T22:37:48.543Z | 02:28 | Benchmark finished | Median times, speedup, efficiency, agreement PASS (max diff 0), host block, 4 workers skipped on 2 CPUs | [16_bench-result.jpg](16_bench-result.jpg) |
| 17 | 2026-10-31T22:37:59.512Z | 02:39 | Cancelled the second benchmark | Status: cancelled; previous table and plots unchanged | [17_bench-cancelled.jpg](17_bench-cancelled.jpg) |
| 18 | 2026-10-31T22:38:10.043Z | 02:50 | Connected mcu0 | Telemetry at 1 Hz with CRC OK badge and sparklines; SIMULATED ribbon | [18_hw-mcu-telemetry.jpg](18_hw-mcu-telemetry.jpg) |
| 19 | 2026-10-31T22:38:21.379Z | 03:01 | Injected timeout x1 then connected jtag0 | Timeline: timeout -> backoff 100 ms -> connected (recovered) | [19_hw-jtag-recovered.jpg](19_hw-jtag-recovered.jpg) |
| 20 | 2026-10-31T22:38:29.668Z | 03:09 | Simulated cable unplug on mcu0 | State pill FAULT, link_lost message with reconnect hint | [20_hw-mcu-fault.jpg](20_hw-mcu-fault.jpg) |
| 21 | 2026-10-31T22:38:35.289Z | 03:15 | Reconnected mcu0 | Back to CONNECTED; sequence restarts on the new session | [21_hw-mcu-reconnected.jpg](21_hw-mcu-reconnected.jpg) |
| 22 | 2026-10-31T22:38:42.777Z | 03:23 | Scrolled to the block diagram | Labelled diagram: USB hub, boards, 3.3 V SPI, J-Link owns MCU SWD only | [22_hw-block-diagram.jpg](22_hw-block-diagram.jpg) |
| 23 | 2026-10-31T22:38:52.933Z | 03:33 | Ran Task F | All four gradients pass |analytic - FD| <= 1e-5; update table; loss falls to ~1e-20 (log scale) | [23_backprop.jpg](23_backprop.jpg) |
| 24 | 2026-10-31T22:39:02.068Z | 03:42 | Ran self-verification | All checks PASS (FEA, imports, parallel agreement, gradient check, mock recovery) | [24_self-verification.jpg](24_self-verification.jpg) |
| 25 | 2026-10-31T22:39:09.563Z | 03:49 | Opened the log console | Timestamped INFO/WARN lines from the C++ backend | [25_log-console.jpg](25_log-console.jpg) |
| 26 | 2026-10-31T22:39:19.403Z | 03:59 | Pressed Quit | Overlay: backend stopped cleanly | [26_quit.jpg](26_quit.jpg) |

## Browser console during the session

Only expected network errors appeared: `409 Conflict` from the telemetry poll right after the simulated cable unplug (step 20, the backend reports `link_lost` by design), and `ERR_CONNECTION_REFUSED` after Quit (step 26, the backend has stopped). There were no JavaScript exceptions.

## Server log excerpt

```
[    0.002] INFO  Unified Engineering Workbench 1.0.0 starting
[    0.002] INFO  web root: web, data: data, logs: <extracted folder>\logs [path redacted]
[    0.003] INFO  listening on http://127.0.0.1:47900/ (process start -> HTTP ready: 76 ms)
[   11.454] INFO  GET /api/health -> 200
[   11.959] INFO  GET /api/system -> 200
[   12.134] INFO  GET /api/examples -> 200
[   25.039] INFO  geometry import: heatsink_fins.stl (33936 bytes)
[   25.041] INFO  heatsink_fins.stl: 156 triangles, closed
[   25.042] INFO  POST /api/geometry/import -> 202
[   55.285] INFO  geometry import: heatsink_fins.stl (33936 bytes)
[   55.285] INFO  heatsink_fins.stl: 156 triangles, closed
[   55.288] INFO  POST /api/geometry/import -> 202
[   63.966] INFO  geometry import: malformed/stl_nan_vertex.stl (111 bytes)
[   63.966] WARN  malformed/stl_nan_vertex.stl: Non-finite value 'nan' in vertex.
[   63.968] INFO  POST /api/geometry/import -> 202
[   72.351] INFO  geometry import: malformed/stl_binary.stl (134 bytes)
[   72.351] WARN  malformed/stl_binary.stl: This is a binary STL file; only ASCII STL is supported.
[   72.353] INFO  POST /api/geometry/import -> 202
[   83.550] INFO  nodal CSV import: heatsink_nodal_temperature_SYNTHETIC.csv (329887 bytes)
[   83.551] INFO  POST /api/results/import -> 202
[   83.554] INFO  heatsink_nodal_temperature_SYNTHETIC.csv: 8274 nodes, T in [51.683, 85] C
[   91.669] INFO  GET /api/results/j5/node/5992 -> 200
[   99.763] INFO  nodal CSV import: malformed/csv_semicolon.csv (47 bytes)
[   99.764] WARN  malformed/csv_semicolon.csv: The file is semicolon-delimited.
[   99.766] INFO  POST /api/results/import -> 202
[  104.963] INFO  POST /api/fea/study -> 202
[  109.319] INFO  POST /api/fea/study -> 202
[  122.839] INFO  POST /api/fea/solve -> 200
[  135.567] INFO  POST /api/fea/study -> 202
[  136.399] INFO  POST /api/fea/study -> 202
[  143.892] INFO  GET /api/system -> 200
[  149.848] INFO  benchmark: 4000 cases x 4000 elements
[  149.849] INFO  POST /api/bench/run -> 202
[  157.051] INFO  benchmark finished; agreement PASS
[  165.382] INFO  benchmark: 4000 cases x 4000 elements
[  165.387] INFO  POST /api/bench/run -> 202
[  168.621] INFO  cancel requested for job j12
[  173.178] INFO  GET /api/hw/devices -> 200
[  177.594] INFO  POST /api/hw/mcu0/connect -> 202
[  177.654] INFO  mcu0 attempt 1: connected
[  186.869] WARN  [SIMULATED] fault armed on jtag0: 1 timeout(s)
[  186.869] INFO  POST /api/hw/jtag0/fault -> 200
[  187.897] INFO  POST /api/hw/jtag0/connect -> 202
[  188.306] WARN  jtag0 attempt 1: [SIMULATED] SEGGER J-Link (JTAG/SWD debug probe) did not answer within 250 ms (injected timeout). -> retry in 100 ms
[  188.306] INFO  jtag0 attempt 2: connected
[  188.306] INFO  jtag0 recovered after 2 attempts
[  198.710] WARN  [SIMULATED] fault armed on mcu0: 0 timeout(s), link drop on next poll
[  198.710] INFO  POST /api/hw/mcu0/fault -> 200
[  198.821] WARN  [SIMULATED] USB link to ST NUCLEO-F446RE (STM32F446RE, Cortex-M4F) lost (cable unplugged?).
[  203.293] INFO  POST /api/hw/mcu0/connect -> 202
[  203.366] INFO  mcu0 attempt 1: connected
[  216.395] INFO  mcu0 disconnected
[  216.397] INFO  POST /api/hw/mcu0/disconnect -> 200
[  216.479] INFO  jtag0 disconnected
[  216.483] INFO  POST /api/hw/jtag0/disconnect -> 200
[  216.580] INFO  POST /api/backprop/run -> 202
[  220.943] INFO  POST /api/backprop/run -> 202
[  226.603] INFO  GET /api/system -> 200
[  230.194] INFO  POST /api/selftest -> 202
[  230.234] INFO  self-verification passed
[  248.443] INFO  shutdown requested from the UI
[  248.443] INFO  POST /api/shutdown -> 200
[  248.508] INFO  stopped cleanly
```
