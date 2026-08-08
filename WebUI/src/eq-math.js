/*
 * Shared parametric EQ math: the exact bell (peaking) filter magnitude used
 * by the Teensy, plus fitting of a small set of peaking filters to a
 * correction curve (the analyzer's "convert diff to EQ").
 */

const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

// Exact bell (peaking EQ) magnitude in dB, from the RBJ analog prototype.
// This is the same math the Teensy uses, so drawn and predicted curves
// match the audible response.
export function peakingBellDb(freq, centerFreq, gain, q) {
  if (!gain || q <= 0 || centerFreq <= 0 || freq <= 0) return 0;
  const A = Math.pow(10, gain / 40);
  const O = freq / centerFreq;
  const c = (1 - O * O) ** 2;
  const num = c + (A * O / q) ** 2;
  const den = c + (O / (A * q)) ** 2;
  return 10 * Math.log10(num / den);
}

// Combined response of a set of {freq, gain, q} points at one frequency.
export function peqSumDb(points, freq) {
  let total = 0;
  for (const p of points) total += peakingBellDb(freq, p.freq, p.gain, p.q);
  return total;
}

// Convert a bandwidth in octaves to the equivalent bell Q.
export const octavesToQ = (octaves) =>
  Math.pow(2, octaves / 2) / (Math.pow(2, octaves) - 1);

/*
 * Fit up to maxBands peaking filters to a correction curve sampled at
 * 1/bandsPerOctave-octave band centers. correction[i] is the desired gain
 * in dB at freqs[i]; NaN marks bands to ignore (out of range or no signal).
 *
 * Greedy: repeatedly place a filter at the largest remaining residual,
 * estimating its width from how far the residual stays above half the peak,
 * then subtract its response. No filter is placed within 1/3 octave of an
 * existing one - opposing near-coincident bells only cancel on paper, and
 * their huge gains make the set fragile and un-editable. A few damped
 * refinement passes afterwards re-balance the gains where filters overlap.
 * boostLimit/cutLimit bound each individual band, not just the sum, so a
 * single band can never exceed the correction budget (boosts eat headroom
 * band-by-band, not sum-wise). Returns [{freq, gain, q}] sorted by frequency.
 */
export function fitPeqPoints(freqs, correction, {
  maxBands = 8,
  minGainDb = 1,
  gainLimit = 15,
  boostLimit = gainLimit,
  cutLimit = gainLimit,
  qMin = 0.4,
  qMax = 10,
  bandsPerOctave = 3,
} = {}) {
  const n = freqs.length;
  const valid = (i) => Number.isFinite(correction[i]);
  const clampGain = (g) => clamp(g, -cutLimit, boostLimit);
  const residual = correction.map((v) => (Number.isFinite(v) ? v : NaN));
  const points = [];
  const nearExisting = (f) =>
    points.some((p) => Math.abs(Math.log2(f / p.freq)) < 1 / 3);

  for (let b = 0; b < maxBands; b++) {
    let idx = -1;
    let peak = 0;
    for (let i = 0; i < n; i++) {
      if (valid(i) && !nearExisting(freqs[i]) && Math.abs(residual[i]) > peak) {
        peak = Math.abs(residual[i]);
        idx = i;
      }
    }
    if (idx < 0 || peak < minGainDb) break;

    const sign = Math.sign(residual[idx]);
    let lo = idx;
    let hi = idx;
    while (
      lo > 0 && valid(lo - 1) &&
      Math.sign(residual[lo - 1]) === sign &&
      Math.abs(residual[lo - 1]) >= peak / 2
    ) lo--;
    while (
      hi < n - 1 && valid(hi + 1) &&
      Math.sign(residual[hi + 1]) === sign &&
      Math.abs(residual[hi + 1]) >= peak / 2
    ) hi++;

    const point = {
      freq: freqs[idx],
      gain: clampGain(residual[idx]),
      q: clamp(octavesToQ(Math.max(1 / 3, (hi - lo + 1) / bandsPerOctave)), qMin, qMax),
    };
    points.push(point);
    for (let j = 0; j < n; j++) {
      if (valid(j)) residual[j] -= peakingBellDb(freqs[j], point.freq, point.gain, point.q);
    }
  }

  // Overlapping bells fight each other; nudge each gain toward the exact
  // correction at its own center to settle the ensemble. Half-steps: full
  // steps oscillate when neighbors overlap strongly, driving coupled bands
  // to opposite clamps.
  for (let pass = 0; pass < 5; pass++) {
    for (const p of points) {
      const i = freqs.indexOf(p.freq);
      if (i < 0 || !valid(i)) continue;
      const err = correction[i] - peqSumDb(points, freqs[i]);
      p.gain = clampGain(p.gain + 0.5 * err);
    }
  }

  return points
    .filter((p) => Math.abs(p.gain) >= minGainDb / 2)
    .sort((a, b) => a.freq - b.freq)
    .map((p) => ({
      freq: Math.round(p.freq * 10) / 10,
      gain: Math.round(p.gain * 10) / 10,
      q: Math.round(p.q * 100) / 100,
    }));
}
