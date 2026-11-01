# Ownership statement

**Candidate:** RA2311033010038

## Reused work (not written for this submission)

| Component | Where | Licence |
|---|---|---|
| cpp-httplib v0.18.3 | `third_party/httplib/` | MIT |
| nlohmann/json v3.11.3 | `third_party/nlohmann/` | MIT |
| doctest v2.4.11 (tests only) | `third_party/doctest/` | MIT |
| three.js r170 and OrbitControls (rendering only) | `src/frontend/vendor/three/` | MIT |
| GCC/MinGW-w64 runtime, statically linked | inside the `.exe` files | GCC RLE / MIT-style |

These are used unmodified and pinned by hash in `DEPENDENCIES.lock`. Standard textbook material
(two-node linear element matrices, Thomas algorithm, IEEE CRC-32, Gauss–Legendre weights, the
adiabatic-tip fin solution) was implemented from the underlying equations, not copied from other code.

## Work created for this submission

Everything else was created for this assignment by the candidate:

* the C++ core, HTTP layer and launcher (`src/core`, `src/app`);
* the CLI (`src/cli`);
* the frontend apart from the vendored three.js (`src/frontend`);
* the tests;
* the example and malformed data;
* the results;
* the build and packaging files;
* the documentation and the block diagram.

Roughly 90% of this work (requirements, architecture, implementation, analysis and verification)
was done by the candidate. An AI coding assistant (Claude Opus 5.5) was used in a supporting role
for debugging, code review, build/test automation, browser testing, documentation proofreading and
automated commits, as disclosed in `docs/AI_USE_LOG.md`. Every AI suggestion was reviewed by the
candidate before being accepted. The candidate is responsible for all of the work. The handwritten
Task F appendix is entirely the candidate's own.

No code was taken from other applicants, from previous submissions, or from any non-open source.
All data is self-created. The nodal CSV is synthetic and is labelled as such everywhere it appears.
