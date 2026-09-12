/*
 * House-curve targets, shared by the analyzer's auto-EQ generator and the
 * auto-FIR wizard's design stage.
 *
 * A target curve is the in-room response the correction aims for, as
 * [frequency, dB] control points interpolated linearly in log-frequency
 * (same scheme as mic cal files). Absolute level is meaningless here -
 * curves are re-centered before use so their median over the level-
 * alignment window is 0, otherwise a curve that sits above or below zero
 * would fight the mic/source alignment and turn into an overall gain.
 *
 * A *selection* is the plain object {mode, tiltDbPerOct, customPoints,
 * customName} both views hold. It lives under one localStorage key, so
 * choosing a house curve in either place applies in the other.
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

/** The tilt mode's pivot: 0 dB here, so a tilt needs no re-centering. */
export const TILT_PIVOT_HZ = 1000;

/** In-room responses corrected dead flat sound bright; this is the house default. */
export const DEFAULT_TILT_DB_PER_OCT = -0.5;

export const DEFAULT_TARGET = { mode: 'tilt', tiltDbPerOct: DEFAULT_TILT_DB_PER_OCT };

/**
 * The target's level, in dB, at each of `freqs` - the one place the four
 * target modes turn into numbers, so the analyzer's preview and the FIR
 * designer aim at exactly the same curve.
 *
 * @param {{mode:string, tiltDbPerOct?:number, points?:Array<[number,number]>,
 *          customPoints?:Array<[number,number]>}} target
 *   'tilt' (tiltDbPerOct, pivoted at 1 kHz) | 'flat' | a TARGET_CURVE_PRESETS
 *   id | 'custom' (points/customPoints, e.g. an imported REW target)
 * @param {Float64Array|number[]} freqs  any ascending frequency grid
 * @param {{loHz?:number, hiHz?:number}} [window]  re-centering window
 * @returns {Float64Array} one value per entry of `freqs`
 */
export function targetDbOnFreqs(target, freqs, { loHz = 200, hiHz = 5000 } = {}) {
  const out = new Float64Array(freqs.length);
  const mode = target?.mode ?? 'flat';

  if (mode === 'flat') return out;
  if (mode === 'tilt') {
    // Pivoted at 1 kHz, where it is 0 by construction - re-centering a tilt
    // would only move the pivot somewhere less predictable.
    const tilt = Number(target.tiltDbPerOct);
    if (!Number.isFinite(tilt)) return out;
    for (let i = 0; i < freqs.length; i++) out[i] = tilt * Math.log2(freqs[i] / TILT_PIVOT_HZ);
    return out;
  }

  const points = mode === 'custom'
    ? (target.points ?? target.customPoints)
    : TARGET_CURVE_PRESETS.find((c) => c.id === mode)?.points;
  // Custom selected with nothing imported yet (or an unknown mode): flat.
  if (!points || points.length < 2) return out;

  // calCurveForGrid only ever reads `.centers`, so a band grid and a bare
  // frequency array are the same input to it - one interpolator for mic cal
  // files, analyzer bands and the FIR designer's log grid alike.
  const curve = calCurveForGrid(points, { centers: freqs });
  const median = medianInWindow(curve, freqs, loHz, hiHz);
  for (let i = 0; i < out.length; i++) out[i] = curve[i] - median;
  return out;
}

// Median of `values` over the frequencies inside [loHz, hiHz]; 0 (i.e. no
// re-centering) when the window catches nothing.
function medianInWindow(values, freqs, loHz, hiHz) {
  const inWindow = [];
  for (let i = 0; i < freqs.length; i++) {
    if (freqs[i] >= loHz && freqs[i] <= hiHz) inWindow.push(values[i]);
  }
  if (!inWindow.length) return 0;
  inWindow.sort((a, b) => a - b);
  const mid = Math.floor(inWindow.length / 2);
  return inWindow.length % 2 ? inWindow[mid] : (inWindow[mid - 1] + inWindow[mid]) / 2;
}

// Interpolate a target curve onto a band grid and re-center it so the
// median over [loHz, hiHz] (the mic/source level-alignment window) is 0.
export function targetCurveForGrid(points, grid, loHz = 200, hiHz = 5000) {
  return Array.from(targetDbOnFreqs({ mode: 'custom', points }, grid.centers, { loHz, hiHz }));
}

/** One-line explanation of a target mode, shown under both selectors. */
export function targetModeHelp(target) {
  const mode = target?.mode;
  return {
    tilt: '0 reproduces the source exactly; negative tilts the target down toward the treble (warmer). In-room responses corrected fully flat often sound bright — −0.5 to −1 is a common preference.',
    flat: 'Corrects the in-room response dead flat. Often sounds bright and thin — most listeners prefer a tilted or Harman-style target.',
    harman: 'Bass shelf rising to +6.5 dB at 20 Hz, gently falling treble — the preferred in-room response from Harman’s listening research.',
    bk: 'Flat through bass and mids, then −1 dB/octave above 400 Hz — B&K’s classic room recommendation.',
    custom: (target?.customPoints ?? target?.points)
      ? 'Imported target, interpolated onto the measured frequencies and re-centered around the mids.'
      : 'Import a REW-style target file (“frequency gain” per line) to use it here.',
  }[mode] ?? '';
}

// --- Persisted selection (shared by AnalyzerView and FirWizardView) ---

export const TARGET_STORAGE_KEY = 'vybes-rta-eq-target';

/**
 * The stored selection, normalized and validated, or null when nothing
 * usable is stored. An unknown mode - or 'custom' with no points - falls
 * back to the default tilt rather than leaving a selector pointing at
 * something that cannot be drawn.
 *
 * @returns {{mode:string, tiltDbPerOct:number, customPoints:Array|null, customName:string}|null}
 */
export function readStoredTarget() {
  let stored;
  try {
    stored = JSON.parse(localStorage.getItem(TARGET_STORAGE_KEY));
  } catch (e) {
    return null; // corrupt storage, or no storage at all (tests, private mode)
  }
  if (!stored || typeof stored.mode !== 'string') return null;

  const customPoints = Array.isArray(stored.customPoints) && stored.customPoints.length >= 2
    ? stored.customPoints
    : null;
  const valid = ['tilt', 'flat', 'custom', ...TARGET_CURVE_PRESETS.map((c) => c.id)];
  const usable = valid.includes(stored.mode) && (stored.mode !== 'custom' || customPoints);
  const tilt = Number(stored.tiltDbPerOct);
  return {
    mode: usable ? stored.mode : DEFAULT_TARGET.mode,
    tiltDbPerOct: Number.isFinite(tilt) ? tilt : DEFAULT_TILT_DB_PER_OCT,
    customPoints,
    customName: typeof stored.customName === 'string' ? stored.customName : (customPoints ? 'stored target' : ''),
  };
}

/** Persist a selection; failure is silent - the choice still applies this session. */
export function writeStoredTarget({ mode, tiltDbPerOct, customPoints = null, customName = '' }) {
  try {
    localStorage.setItem(
      TARGET_STORAGE_KEY,
      JSON.stringify({ mode, tiltDbPerOct, customPoints, customName })
    );
  } catch (e) { /* storage full or blocked */ }
}
