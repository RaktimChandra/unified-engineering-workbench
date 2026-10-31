# AI-use log

The brief permits AI assistance and asks for disclosure. This log records how AI was used, what was
checked independently, and where AI output was wrong or unsuitable.

## 1. Tools and models

| Tool | Model | Used for |
|---|---|---|
| Claude (Anthropic), agentic workspace with a Linux sandbox | Claude Opus 5.5 | Planning; writing the C++ core, HTTP layer, frontend, tests, CLI and docs; running builds, tests and benchmarks; cross-compiling and Wine launch checks |
| Same tool, parallel sub-agents of the same model | Claude Opus 5.5 | Frontend implementation and UI testing; embedded-design write-up and block diagram; demo walkthrough capture |

No paid solver, CAD tool or hardware was used. All numbers in `results/` were produced by the
project's own C++ code (`wb_cli`), not by the AI.

## 2. How the work was directed

The candidate supplied the assignment PDF, the job description, the evaluation rubric and the
submission constraints: Windows `Launch.exe`, C++17 backend, a 1 MB PDF upload limit, and the
deadline. The candidate asked for complete coverage of Tasks A–F with extra features only where
they are verified, since the rubric gives no credit for unsupported feature count. The working
pattern was **plan → implement → verify against an independent oracle → record evidence**.

### Representative prompts (excerpts)

1. Candidate, initial request (paraphrased): *"Go through the JD, the assignment and the form
   properly. Produce the best possible complete solution, with every artefact the brief asks for
   and extras that suit it, no compromise. Explain what is to be done, then complete everything."*
2. Candidate, refinement: *"Plan beyond what they ask. Explain exactly what I must write by hand,
   why and how."*
3. Lead to the frontend sub-agent (excerpt): *"Build the complete web UI in src/frontend only … use
   exactly the HTTP API in src/app/api.cpp … every plot must have axis labels with units and a
   legend … test every view with Playwright against the real backend and report backend bugs instead
   of editing backend files."*
4. Lead to the embedded sub-agent (excerpt): *"Boards must match the C++ mocks … interface/pin/voltage
   table with owner/master column … explain why a JTAG adapter is a debug interface, not a compute
   board … flag every pin fact you are unsure of."*

### Iterative refinement (examples)

* The benchmark design was revised after measuring (section 4, item 2).
* The batch generator changed from a log-uniform to a linear draw for cross-compiler determinism
  (section 4, item 3).
* The board wiring changed after the embedded review (section 4, items 4 and 5).

## 3. Independent verification (not "the AI said so")

| Claim | Independent check |
|---|---|
| FEA solver is correct | Analytic solution; midpoint error must equal qh²/(8k) **exactly** (1.25, 0.3125, 0.078125, 0.01953125 °C); L2 error must equal 500h²/√30; observed rate 2.0000; energy balance R₀ + R_N = −qAL; Thomas cross-checked with dense Gaussian elimination |
| Backprop gradients | Central finite differences (ε = 1e-6), error ≤ 1e-5; values re-derived by hand on paper (appendix) |
| Parallel = serial | Max \|ΔT\| over all 16 M nodal values = 0 (bitwise); buffer poisoned with −1 before each run so stale data cannot pass |
| Generator is portable | Golden value of `mt19937_64` taken from the C++ standard ([rand.predef]) |
| Parsers reject bad data | 28 malformed inputs with exact expected code and line number |
| Windows launch | Statically linked MinGW build; DLL imports inspected with `objdump` (system DLLs only); `Launch.exe` and `wb_tests.exe` executed under Wine (60/60 pass) |
| Portability | Warning-free builds on GCC 13, Clang 18 and MinGW-w64 |

## 4. Incorrect or unsuitable AI output that was identified and corrected

1. **Wrong sign in an AI-written test.** The first energy-balance test asserted `R0 = +kA·T'(0)`. Running
   the suite failed: −0.42 W was expected as +0.42 W. Analysis showed the solver was right. `R = KT − F`
   is the heat *supplied* at the node, and the Fourier inflow at x = 0 is −kA·T'(0). The test was
   corrected to check sign and magnitude. *Lesson: a failing test can be the defect, so check the
   physics before "fixing" the code.*
2. **Misleading benchmark.** The first harness allocated a new 128 MB result array inside every timed
   run. Two-worker speedup came out at only 1.39× and 1.71× with a large spread. Moving allocation and
   first-touch out of the timed region gave 1.90–1.95×. The pre-fix CSVs are kept in `results/dev/`
   as evidence.
3. **Hidden portability bug.** A log-uniform draw for k needed `std::pow`, whose last bit may differ
   between C runtimes. That would quietly break the "identical cases on every compiler" requirement.
   It was replaced by a linear mapping using only IEEE-exact operations.
4. **Unsuitable wiring instruction.** The lead's initial instruction connected the J-Link to the NUCLEO
   CN4 header. The embedded review found that CN4 is the ST-LINK's SWD *output* for external targets,
   so it cannot reach the on-board STM32. The design now uses the MCU's own debug pins
   (PA13/PA14/PA15/PB3/PB4/NRST) with the CN2 jumpers removed.
5. **Pin conflict.** The first mock capability string placed SPI1 SCK on PA5, which is also the LD2
   user LED on the NUCLEO-F446RE. It was moved to SPI2 (PB13/14/15, CS on PB12) in both the design and
   the code.
6. **UI defects found by automated browser testing:**
   * a CSS rule overrode the `hidden` attribute;
   * node picking was listening on the colour-bar canvas instead of the 3-D canvas;
   * fixed-viewBox plots shrank their text;
   * 1 Hz telemetry polling flooded the log console.

## 5. Candidate's own review and edits

*This section is written by the candidate.*

- [ ] Re-derived Task F by hand (appendix) and compared it with `results/backprop_gradcheck.csv`.
- [ ] Recomputed the N = 10 midpoint error by hand: q·h²/(8k) = 10⁴·0.01/80 = 1.25 °C.
- [ ] Ran `Launch.exe` on own Windows 11 laptop and re-ran the benchmark there (results attached if done).
- Edits made: …
- Suggestions rejected or changed: …

## 6. Interaction record

The full conversation and tool log are available on request. They are not reproduced here because of
their length.
