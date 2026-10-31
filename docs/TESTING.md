# Testing

`wb_tests` (doctest) contains **60 test cases and about 13.7 k assertions** in 7 suites. Run it with
`ctest --test-dir build --output-on-failure`, or run `app\bin\wb_tests.exe` from the extracted ZIP.

| Suite | Cases | Kind | Highlights |
|---|---:|---|---|
| `stl` | 8 | positive, negative, limits | closed tetrahedron: exact area and volume; heat-sink example volume = 28,800 mm³; CRLF/BOM/case/`+` accepted; 14 malformed inputs with exact code and line; binary detection with a `solid` header; size and triangle limits; cancellation |
| `nodal_csv` | 7 | positive, negative | reordered/quoted columns; extra columns; 14 malformed inputs with code and line; mm-unit warning; node limit |
| `fea1d` | 11 | numerical | exact solution satisfies the ODE and BCs; element matrices for N = 4; zero row sums; BCs exact for N = 1..80; nodal error < 1e-11; midpoint error = qh²/(8k); L2 error = 500h²/√30; observed rate 2; energy balance (−0.42 W, −0.58 W); Thomas = dense Gauss; fused kernel bitwise equal; invalid input |
| `parallel` | 9 | numerical, concurrency | exact [0,1) mapping; reproducible generator; golden `mt19937_64` value from the C++ standard; each index visited once; work runs on more than one thread; serial = parallel bitwise for 1/2/4 workers; cancellation leaves the pool reusable; harness capping and agreement |
| `hardware_mocks` | 8 | behaviour, failure | CRC32 check value 0xCBF43926; three device kinds; the JTAG probe has no compute capability; connect/telemetry/disconnect; timeout → backoff 100/200 ms → recovered; persistent failure gives up cleanly; link loss → reconnect; unknown ids |
| `backprop` | 6 | numerical | forward pass and the four gradients match the handwritten values; central difference within 1e-5 at two points; simultaneous update; 100-step monotone loss decrease |
| `api` | 11 | HTTP integration | real server and client on 127.0.0.1: health, imports, failure without a partial blob, path-traversal refusal, exact node inspection, FEA validation (bad value, bad type, bad JSON), exports, benchmark cancel then recovery, oversized-workload refusal, hardware recovery, self-test all pass |

## Outcomes (this submission)

| Build | Platform | Result |
|---|---|---|
| GCC 13.3.0, Release | Linux x86-64 | 60/60 passed |
| Clang 18.1.3, Release | Linux x86-64 | 60/60 passed, zero warnings |
| MinGW-w64 GCC 13, static | Windows x64 binary executed under Wine 9.0 | 60/60 passed |
| MinGW-w64 GCC 13, packaged `Launch.exe` | Windows 11 laptop (Ryzen 7 6800H) | launched; Performance benchmark agreement PASS (p = 1, 2, 4, 8) |

All three builds use `-Wall -Wextra -Wpedantic -Wshadow` and produce zero warnings.

## Defects found by the tests during development

See `docs/dev/build_notes.md`. In short:

* The first energy-balance test had the wrong sign convention. The solver was correct.
* The first benchmark timed region included 128 MB first-touch page faults.
* The frontend agent found and fixed four UI defects through Playwright runs.

## Manual and UI checks

* Every malformed file in `data/malformed/` was loaded through the UI. Each shows an error card
  with the code, line and hint, and the previously displayed model is kept
  (see `docs/walkthrough/`).
* Cancelling a benchmark from the status bar leaves the last completed result on screen.
* Built-in self-verification (**Verify** view) passes 13/13 inside the packaged exe.
