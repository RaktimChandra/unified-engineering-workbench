# Unified Engineering Workbench

A small engineering workbench for the PASS Science Works R&D internship assignment. It imports
ASCII STL geometry and ANSYS-format nodal temperature CSVs and solves a 1-D thermal FEA problem
with verified convergence. It also benchmarks serial against multi-threaded batch solves, models a
microcontroller/FPGA/JTAG hardware bench with simulated adapters, and checks a hand-derived
backpropagation step. All backend logic, numerics, file processing and concurrency are C++17. The
user interface is a local web page served by the same executable.

> Candidate ID: RA2311033010038 · Target: Windows 11 x64 · Version 1.0.0

---

## 1. One-click start (Windows 11 x64)

1. Extract `RA2311033010038_Workbench.zip` anywhere, for example to the Desktop. Keep the folder structure.
2. Double-click **`Launch.exe`**.
3. Your default browser opens the workbench at `http://127.0.0.1:47615/`. If that port is busy, the next free port is used.

You don't need a terminal, administrator rights, an installer, internet access or a first-run build.
`Launch.exe` is statically linked and imports only DLLs that ship with Windows (`KERNEL32`, `msvcrt`,
`SHELL32`, `USER32`, `WS2_32`). It listens on the loopback address only, so the Windows Firewall
prompt does not appear.

**Stopping:** click **Quit** in the header, or close the browser tab. After a tab close the program
exits on its own about 15 s later; a reload within that window keeps it running.

**First launch of a downloaded ZIP:** Windows SmartScreen may show "Windows protected your PC" because
the executable is not code-signed. Click **More info → Run anyway**. This is the only manual step, and
it is a property of unsigned binaries, not of the application.

Logs are written to `logs\workbench.log` next to `Launch.exe`, or to `%TEMP%\UnifiedEngineeringWorkbench\`
if that folder is read-only. The same lines appear in the in-app **Log** console.

### From VS Code or Antigravity (one action)

Open the extracted folder as the workspace, then use either option:

| Option | Steps | Prerequisite |
|---|---|---|
| Run and Debug | Select **"Workbench: packaged Launch.exe"** → press **F5** | VS Code C/C++ extension `ms-vscode.cpptools` (the `cppvsdbg` debugger type) |
| Task (no extension) | **Terminal → Run Task → "Workbench: run packaged Launch.exe"** | none; this also works in Antigravity, whose extension marketplace may not carry cpptools |

Both start the same packaged `Launch.exe`. Neither needs a build or internet access. The standalone launcher does not need the editor.

---

## 2. What is in the application

| View | Task | What you can do |
|---|---|---|
| **Geometry** | B | Import an ASCII STL or a nodal-temperature CSV (your own file or a bundled example, including malformed ones). STL: rotate, pan, zoom, fit-to-view, preset views, mesh diagnostics (closed/open, area, volume, flipped normals), units mm/m/in. CSV: coloured 3-D point cloud with a °C colour bar, click-to-inspect nodes (exact values served by C++). |
| **Thermal Analysis** | C | Edit the rod problem (Task C defaults locked behind "Reset"). Run the N = 10/20/40/80 study, plus extra meshes. View T(x) against the exact solution, midpoint error, log-log convergence with fitted rate, energy balance, and the assembled K and F for small N. Export CSV and SVG. |
| **Performance** | D | Seeded batch of independent rod solves, run serial and on a bounded `std::thread` pool with 1, 2 and 4 workers (capped at logical CPUs). Shows warm-up and 5 timed runs, median, speedup, efficiency, serial/parallel agreement ≤ 1e-8 °C, host and compiler report, and launch-to-ready time. Long runs can be cancelled. |
| **Hardware** | E | Simulated NUCLEO-F446RE, Arty A7-35T and J-Link: discovery, capabilities, connect/disconnect and 1 Hz telemetry with CRC32. Fault injection: connect timeout (recovered by retry with backoff) and cable unplug (recovered by reconnect). Shows the block diagram. Every simulated element is labelled. |
| **Backprop** | F | Forward pass, analytic gradients against central differences (ε = 1e-6), one simultaneous update, and 100-step loss curve with exports. |
| **Verify** | all | Runs 13 numerical and robustness checks inside the shipped executable: FEA exactness and rates, energy balance, Thomas against dense Gauss, parser rejection, parallel agreement, gradient check and fault recovery. |

Long operations run as background jobs with progress, cancel, and a status bar. A cancelled job never
publishes a partial result, and the previous result stays on screen.

---

## 3. Architecture

```
 Browser (src/frontend)          Launch.exe (C++17)
 ─────────────────────           ───────────────────────────────────────────────
 HTML/CSS/ES modules             src/app      transport & presentation glue
 three.js (vendored, r170)  ──►    main.cpp      start-up, port, browser, lifetime
 own SVG plotting                  api.cpp       REST routes → wb_core calls
 job polling / cancel        ◄──   jobs.cpp      bounded executor, atomic publish
          HTTP on 127.0.0.1        json_codec    core types ↔ JSON
                                  src/core    wb_core static library (no I/O, no HTTP)
                                    stl, nodal_csv   parsing + validation
                                    fea1d            assembly, BCs, Thomas, errors
                                    parallel         generator, worker pool
                                    benchmark/sysinfo timing + host report
                                    hw/              adapter interface + mocks
                                    backprop, report numerics, CSV/SVG writers
                                  src/cli     wb_cli: regenerates data/ and results/
```

* **Separation of concerns.** `wb_core` has no dependency on HTTP, JSON or the UI. It reports
  progress through callbacks and supports cooperative cancellation through a token, so the same code
  serves the GUI, the CLI and the tests.
* **Why a local web UI?** It gives one portable interface with a mature 3-D renderer (three.js) while
  keeping every computation in C++. The browser only draws what the backend returns.
* **Determinism.** Batch cases come from `std::mt19937_64`, whose output the C++ standard fixes, and a
  hand-written mapping to [0, 1). `std::uniform_real_distribution` is avoided because its output
  differs between standard libraries.

More detail is in `docs/report/Report.pdf`.

---

## 4. Supported formats

| Format | Accepted | Rejected with line number and fix hint |
|---|---|---|
| **ASCII STL** (`solid … endsolid`) | any case of keywords, CRLF/LF, UTF-8 BOM, `+` signs, multiple solids, zero normals | empty file, binary STL (detected even if its header says `solid`), NaN/Inf/overflow, decimal comma, missing or extra values, quads, truncated file, trailing garbage, files > 256 MiB or > 5 M triangles |
| **Nodal CSV** `node_id,x_m,y_m,z_m,temperature_C` | columns matched by name in any order, quotes, BOM, CRLF, blank lines, extra columns (ignored with a warning) | empty file, header only, missing or duplicated columns, missing header, semicolon export, non-finite values, non-integer or non-positive or duplicate `node_id`, wrong field count, T < −273.15 °C, > 256 MiB or > 5 M nodes |

Units: STL files carry no unit, so the user chooses it at import (default mm). The CSV schema uses SI
metres and °C. Tested size limits are in `docs/file_limits.md`. Native ANSYS or CAD formats are out of scope.

Example data is self-created and described in `data/PROVENANCE.md`. The nodal CSV is **synthetic** and is
labelled as such in its file name, in the manifest and in the UI. It is not an ANSYS export.

---

## 5. Build from source and run the tests

See `docs/BUILD.md` for full instructions. Short version:

```bat
:: Windows, Visual Studio 2022 (MSVC) or any CMake + C++17 toolchain
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build\Release\Launch.exe          :: finds src\frontend and data\ in the source tree
```

```sh
# Linux / macOS
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
ctest --test-dir build --output-on-failure
./build/wb_cli fea results && ./build/wb_cli backprop results && ./build/wb_cli bench results
```

The tests can also be run without building: `app\bin\wb_tests.exe`, run from the extracted folder.
They comprise 60 doctest cases: positive, negative, numerical and HTTP integration tests. They pass
on GCC 13, Clang and MinGW-w64 (the Windows binary was run under Wine). See `docs/TESTING.md`.

---

## 6. Folder layout

| Path | Contents |
|---|---|
| `Launch.exe`, `app/web/`, `app/bin/` | packaged application, its UI files, `wb_cli.exe`, `wb_tests.exe` |
| `src/core/` | C++17 numerical and parsing library (`include/wb/*.hpp`, `src/*.cpp`) |
| `src/app/` | HTTP transport, job manager, launcher |
| `src/cli/` | headless command-line front end |
| `src/frontend/` | web UI source; `vendor/three/` holds three.js r170 (MIT) |
| `tests/` | doctest suites |
| `third_party/` | pinned header-only libraries with their licences |
| `data/` | example STL and CSV, malformed corpus, `manifest.json`, `PROVENANCE.md` |
| `results/` | raw CSVs and labelled SVG/PNG plots for Tasks C, D and F; `dev/` keeps pre-fix measurements |
| `docs/` | report, embedded design, build/testing/limits notes, AI-use log, ownership statement, walkthrough |
| `vcs/` | Git bundle and readable commit log |
| `cmake/`, `packaging/`, `scripts/` | cross-compile toolchain, Windows resources, packaging script |
| `.vscode/` | run configurations (also read by Antigravity) |
| `CMakeLists.txt`, `DEPENDENCIES.lock` | build definition and pinned dependency versions and hashes |

All paths in the project are relative, and no credentials or personal data are stored.

---

## 7. Known limitations

* **Benchmarks:** the committed results were measured on a 2-vCPU Linux container (GCC 13, `-O3`).
  The 4-worker case is therefore skipped and disclosed. The Performance view reproduces the
  measurement on any machine in one click and exports the same files.
* **Windows testing:** `Launch.exe` was verified under Wine 9.0. The Windows-specific code paths are
  small (`ShellExecuteW`, `GetModuleFileNameW`, `GetLogicalProcessorInformationEx`, `RtlGetVersion`).
  Behaviour on a physical Windows 11 machine should match, but it was not observed by the build machine.
* **Binary STL** is detected and rejected with guidance rather than parsed, per the brief's ASCII scope.
* **Rendering:** very large meshes (over about 2 M triangles) render slowly on integrated GPUs. Parsing
  stays fast (see `docs/file_limits.md`).
* **Hardware:** all hardware behaviour is simulated. Pinouts and IDCODEs are documented expectations
  from public datasheets and have not been verified on real boards.
* **Code signing:** the executable is unsigned, so SmartScreen shows a warning on first run.
* **Auto-exit:** browsers throttle timers in background tabs, so the "no heartbeat" shutdown waits
  10 minutes. A closed tab is detected within about 15 s through a page-hide beacon.
