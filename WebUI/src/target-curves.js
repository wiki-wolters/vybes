/*
 * House-curve targets for the auto-EQ generator.
 *
 * A target curve is the in-room response the correction aims for, as
 * [frequency, dB] control points interpolated linearly in log-frequency
 * (same scheme as mic cal files). Absolute level is meaningless here -
 * curves are re-centered before use so their median over the level-
 * alignment window is 0, otherwise a curve that sits above or below zero
 * would fight the mic/source alignment and turn into an overall gain.
 */
import { calCurveForGrid } from './rta.js';

export const TARGET_CURVE_PRESETS = [
  {
    id: 'harman',
    label: 'Harman room',
    // Approximation of the preferred in-room steady-state response from
    // Harman's listening research (Olive/Toole): a bass shelf reaching
    // about +6.5 dB at 20 Hz and a gently falling treble.
    points: [
      [20, 6.6], [32, 6.2], [50, 5.2], [80, 3.9], [125, 2.5], [200, 1.3],
      [315, 0.6], [500, 0.1], [1000, 0], [2000, -0.6], [4000, -1.6],
      [8000, -2.9], [12000, -3.8], [16000, -4.6], [20000, -5.2],
    ],
  },
  {
    id: 'bk',
    label: 'B&K room',
    // The classic B&K recommendation: flat through bass and mids, then
    // -1 dB/octave above 400 Hz (log-linear interpolation makes the last
    // segment exactly that slope).
    points: [[20, 0], [400, 0], [20000, -5.64]],
  },
];

// Interpolate a target curve onto a band grid and re-center it so the
// median over [loHz, hiHz] (the mic/source level-alignment window) is 0.
export function targetCurveForGrid(points, grid, loHz = 200, hiHz = 5000) {
  const curve = calCurveForGrid(points, grid);
  const inWindow = [];
  for (let i = 0; i < grid.centers.length; i++) {
    if (grid.centers[i] >= loHz && grid.centers[i] <= hiHz) inWindow.push(curve[i]);
  }
  if (!inWindow.length) return curve;
  inWindow.sort((a, b) => a - b);
  const mid = Math.floor(inWindow.length / 2);
  const median =
    inWindow.length % 2 ? inWindow[mid] : (inWindow[mid - 1] + inWindow[mid]) / 2;
  return curve.map((v) => v - median);
}
