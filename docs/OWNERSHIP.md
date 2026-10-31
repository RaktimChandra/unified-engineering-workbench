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

These are used unmodified and are pinned by hash in `DEPENDENCIES.lock`. Standard textbook material
(two-node linear element matrices, Thomas algorithm, IEEE CRC-32, Gauss–Legendre weights, the
adiabatic-tip fin solution) was implemented from the underlying equations. It was not copied from
other code.

## Work created for this submission

Everything else was created for this assignment. That covers:

* the C++ core, HTTP layer and launcher (`src/core`, `src/app`);
* the CLI (`src/cli`);
* the frontend apart from the vendored three.js (`src/frontend`);
* the tests;
* the example and malformed data;
* the results;
* the build and packaging files;
* the documentation and the block diagram.

It was developed by the candidate with an AI coding assistant (Claude Opus 5.5), as disclosed in
`docs/AI_USE_LOG.md`. The candidate set the requirements and priorities, directed the work, verified
the results and is responsible for all of it. The handwritten Task F appendix is the candidate's own handwritten work.

No code was taken from other applicants, from previous submissions, or from any non-open source.
All data is self-created. The nodal CSV is synthetic and is labelled as such everywhere it appears.
