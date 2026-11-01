# AI-use disclosure

The brief permits AI assistance and asks for disclosure. This document records how AI was used, what was checked independently, and where AI suggestions were wrong or unsuitable.

## 1. Division of work

Roughly 90% of the work (design, implementation, analysis and verification) was done by me, the candidate. AI was used as a supporting tool, not as the author.

| Area | Candidate | AI assistant (Claude Opus 5.5, Anthropic) |
|---|---|---|
| Requirements & scope | Read the assignment, JD and rubric; defined coverage of Tasks A–F; set all constraints (Windows `Launch.exe`, C++17 backend, 1 MB PDF limit, deadline) | — |
| Architecture & design | Designed the FEA solver, backprop module, parallel engine, parsers, HTTP API, web UI and embedded design | Reviewed designs and flagged possible issues |
| Implementation | Wrote the C++ backend, frontend and documentation | Debugging help, code review, small fixes, boilerplate |
| Testing & verification | Designed the verification strategy and oracles; hand-derived results | Ran builds, test suites and browser tests on request |
| Repository workflow | Reviewed all changes | Automated commits and pushes |

No paid solver, CAD tool or hardware was used. All numbers in `results/` were produced by the project's own C++ code (`wb_cli`), not by the AI.

## 2. How AI was used

- **Debugging:** diagnosing failing tests, build errors and runtime defects.
- **Code review:** second opinion on correctness, portability and edge cases.
- **Automation:** running builds, test suites, benchmarks and Playwright browser tests; committing and pushing to the repository.
- **Documentation polish:** proofreading and formatting of docs I wrote.
- **Embedded design review:** checking my board wiring and pin assignments against the NUCLEO-F446RE documentation.

Every AI suggestion was reviewed by me before being accepted.

## 3. Independent verification (not "the AI said so")

| Claim | Independent check |
|---|---|
| FEA solver is correct | Analytic solution: midpoint error must equal qh²/(8k) exactly (1.25, 0.3125, 0.078125, 0.01953125 °C); L2 error must equal 500h²/√30; observed rate 2.0000; energy balance R₀ + R_N = −qAL; Thomas algorithm cross-checked against dense Gaussian elimination |
| Backprop gradients | Central finite differences (ε = 1e-6), error ≤ 1e-5; values re-derived by hand on paper (appendix) |
| Parallel = serial | Max \|ΔT\| over all 16 M nodal values = 0 (bitwise); buffer poisoned with −1 before each run so stale data cannot pass |
| Generator is portable | `mt19937_64` golden value taken from the C++ standard ([rand.predef]) |
| Parsers reject bad data | 28 malformed inputs, each with an exact expected error code and line number |
| Windows launch | Statically linked MinGW build; DLL imports inspected with `objdump` (system DLLs only); `Launch.exe` and `wb_tests.exe` run under Wine (60/60 pass) and on physical Windows 11 hardware |
| Portability | Warning-free builds on GCC 13, Clang 18 and MinGW-w64 |

## 4. AI suggestions that were wrong or unsuitable, and how they were handled

1. **Wrong sign in a suggested test.** An AI-suggested energy-balance assertion used R₀ = +kA·T′(0) and failed (−0.42 W vs +0.42 W). I checked the physics: the solver was correct. R = KT − F is the heat supplied at the node, and the Fourier inflow at x = 0 is −kA·T′(0). The test was corrected to check both sign and magnitude. *Lesson: a failing test can itself be the defect, so verify the physics before changing the code.*
2. **Misleading benchmark.** The first benchmark harness allocated a new 128 MB result array inside every timed run, giving only 1.39× and 1.71× two-worker speedup with high variance. Moving allocation and first-touch out of the timed region gave 1.90–1.95×. Pre-fix CSVs are kept in `results/dev/` as evidence.
3. **Hidden portability bug.** A log-uniform draw for k relied on `std::pow`, whose last bit can differ between C runtimes, which would silently break the "identical cases on every compiler" requirement. It was replaced with a linear mapping using only IEEE-exact operations.
4. **Unsuitable wiring suggestion.** A suggestion to connect the J-Link to the NUCLEO CN4 header was rejected: CN4 is the ST-LINK's SWD output for external targets and cannot reach the on-board STM32. The design uses the MCU's own debug pins (PA13/PA14/PA15/PB3/PB4/NRST) with the CN2 jumpers removed.
5. **Pin conflict.** SPI1 SCK on PA5 conflicts with the LD2 user LED on the NUCLEO-F446RE. It was moved to SPI2 (PB13/14/15, CS on PB12) in both the design and the code.
6. **UI defects found during automated browser testing:**
   - a CSS rule overrode the `hidden` attribute;
   - node picking listened on the colour-bar canvas instead of the 3-D canvas;
   - fixed-viewBox plots shrank their text;
   - 1 Hz telemetry polling flooded the log console.

## 5. Candidate's own verification

- [x] Re-derived Task F by hand (handwritten appendix) and compared it with `results/backprop_gradcheck.csv`.
- [x] Recomputed the N = 10 midpoint error by hand: q·h²/(8k) = 10⁴ × 0.01 / 80 = 1.25 °C, matching `results/fea_convergence.csv`.
- [x] Ran the packaged `Launch.exe` on a Windows 11 laptop (AMD Ryzen 7 6800H) and exported the Task D benchmark (`results/windows11_ryzen7_6800h/`). This run exposed the AMD CPUID padding bug documented in `docs/dev/build_notes.md`.

## 6. Interaction record

The full AI conversation and tool log are available on request. They are not reproduced here because of their length.
