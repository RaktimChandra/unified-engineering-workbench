# Development notes (chronological)

- 2026-11-01 first full test run: 59/60 cases passed. The failing case was the
  energy-balance test itself: it asserted R0 = +kA T'(0), but R = K T - F is the
  heat *supplied* at the node, and with Fourier's law the inflow at x = 0 is
  -kA T'(0) = -0.42 W. The solver was right and the test's sign convention was
  wrong; the test now checks both sign and magnitude (-0.42 W, -0.58 W, sum -1 W = -qAL).

- 2026-11-01 benchmark, first version (4000 cases x 4000 elements, 2 logical CPUs):
  2-worker speedup only 1.39 and 1.71 in two runs (results/dev/benchmark_before_firsttouch_fix_run*.csv),
  with large run-to-run spread. Cause: each timed repetition allocated a fresh
  128 MB result vector, so the timed region included zero-filling and first-touch
  page faults, which the kernel serialises and which do not scale with threads.
  Fix: allocate once with par::prepare(), touch it during warm-up, reuse it across
  repetitions, and poison the parallel buffer with -1 before every run so a stale
  buffer can never pass the agreement check. Result: 1.90-1.95x on 2 workers.
