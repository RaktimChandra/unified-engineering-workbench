# Development notes (chronological)

- 2026-11-01 first full test run: 59/60 cases passed. The failing case was the
  energy-balance test itself: it asserted R0 = +kA T'(0), but R = K T - F is the
  heat *supplied* at the node, and with Fourier's law the inflow at x = 0 is
  -kA T'(0) = -0.42 W. The solver was right and the test's sign convention was
  wrong; the test now checks both sign and magnitude (-0.42 W, -0.58 W, sum -1 W = -qAL).
