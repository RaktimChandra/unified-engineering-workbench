// Viridis colormap (perceptually uniform, colour-blind friendly). Control
// points sampled from matplotlib's viridis at 10 % steps; linear interpolation
// in between is visually indistinguishable at this density.
const VIRIDIS = [
  [0.267004, 0.004874, 0.329415], [0.282623, 0.140926, 0.457517], [0.253935, 0.265254, 0.529983],
  [0.206756, 0.371758, 0.553117], [0.163625, 0.471133, 0.558148], [0.127568, 0.566949, 0.550556],
  [0.134692, 0.658636, 0.517649], [0.266941, 0.748751, 0.440573], [0.477504, 0.821444, 0.318195],
  [0.741388, 0.873449, 0.149561], [0.993248, 0.906157, 0.143936],
];

/** Maps t in [0,1] to [r,g,b] in [0,1]. */
export function viridis(t) {
  if (!Number.isFinite(t)) t = 0;
  t = Math.min(1, Math.max(0, t));
  const f = t * (VIRIDIS.length - 1);
  const i = Math.min(VIRIDIS.length - 2, Math.floor(f));
  const u = f - i;
  const a = VIRIDIS[i], b = VIRIDIS[i + 1];
  return [a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u, a[2] + (b[2] - a[2]) * u];
}

/** Paints a vertical colour bar (max at top) into a canvas. */
export function paintColorbar(canvas) {
  const h = 256;
  canvas.width = 1; canvas.height = h;
  const ctx = canvas.getContext("2d");
  const img = ctx.createImageData(1, h);
  for (let y = 0; y < h; y++) {
    const [r, g, b] = viridis(1 - y / (h - 1));
    img.data.set([r * 255, g * 255, b * 255, 255], y * 4);
  }
  ctx.putImageData(img, 0, 0);
}
