/*
 * Shared parametric EQ math: the exact bell (peaking) filter magnitude used
 * by the Teensy, the dynamic-EQ volume law and loudness compensation
 * (docs/DYNAMIC_EQ.md), plus fitting of a small set of peaking filters to a
 * correction curve (the analyzer's "convert diff to EQ").
 */

import { DEVICE_SAMPLE_RATE } from './device.js';

const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

// Exact bell (peaking EQ) magnitude in dB as the device runs it: the RBJ
// audio-EQ-cookbook peaking biquad at the device sample rate.
//
// The Teensy's SVF is the bilinear transform of the analog prototype
//   H(s) = (s^2 + s*(A/Q) + 1) / (s^2 + s/(A*Q) + 1),  A = 10^(gain/40)
// with only the center frequency prewarped, so its response is that
// prototype evaluated at the warped ratio tan(pi*f/fs) / tan(pi*fc/fs)
// rather than f/fc. Below ~3 kHz the two are indistinguishable; higher up
// the digital bell is narrower than the textbook one and always returns to
// 0 dB at fs/2 (1.2 dB apart at 10 kHz Q4, 2.6 dB at 15 kHz Q10). REW's
// Generic equaliser predicts this same digital curve, so drawn, fitted,
// REW-predicted and audible responses all agree.
//
// centerFreq is capped the way the device caps it (0.49*fs), so the curve
// is the band that actually runs.
export function peakingBellDb(freq, centerFreq, gain, q, sampleRate = DEVICE_SAMPLE_RATE) {
  if (!gain || q <= 0 || centerFreq <= 0 || freq <= 0) return 0;
  if (freq >= sampleRate / 2) return 0; // a bilinear bell is exactly flat at Nyquist
  const fc = Math.min(centerFreq, 0.49 * sampleRate);
  const A = Math.pow(10, gain / 40);
  const O = Math.tan(Math.PI * freq / sampleRate) / Math.tan(Math.PI * fc / sampleRate);
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

/* ── Dynamic EQ (docs/DYNAMIC_EQ.md) ─────────────────────────────────── */

// The master volume slider's percent as gain in dB. The Teensy cubes the
// linear 0-1 value, so a percent is 3 * 20*log10(pct/100) dB: 100% = 0,
// 79% = -6, 50% = -18, 25% = -36. Floored at -60 dB (10% and below), which
// is the bottom of the useful range and keeps 0% finite.
export function volumePctToDb(pct) {
  if (!(pct > 0)) return -60;
  return Math.max(-60, 60 * Math.log10(pct / 100));
}

/*
 * RBJ audio-EQ-cookbook high shelf magnitude in dB, at the device sample
 * rate, with shelf slope S = 1 (alpha = sin(w0)/sqrt(2), i.e. Q = 1/sqrt(2)
 * - the steepest slope that stays monotonic). This is the loudness
 * compensation's only building block, so it is the digital shelf the Teensy
 * runs, not the analog prototype: drawn and audible have to agree.
 */
export function highShelfDb(f, fc, gainDb, fs = DEVICE_SAMPLE_RATE) {
  if (!gainDb || fc <= 0 || f <= 0) return 0;
  const A = Math.pow(10, gainDb / 40);
  const w0 = 2 * Math.PI * Math.min(fc, 0.49 * fs) / fs;
  const cosW0 = Math.cos(w0);
  const alpha = Math.sin(w0) / Math.SQRT2;
  const shelfTerm = 2 * Math.sqrt(A) * alpha;
  const b = [
    A * ((A + 1) + (A - 1) * cosW0 + shelfTerm),
    -2 * A * ((A - 1) + (A + 1) * cosW0),
    A * ((A + 1) + (A - 1) * cosW0 - shelfTerm),
  ];
  const a = [
    (A + 1) - (A - 1) * cosW0 + shelfTerm,
    2 * ((A - 1) - (A + 1) * cosW0),
    (A + 1) - (A - 1) * cosW0 - shelfTerm,
  ];
  const w = 2 * Math.PI * f / fs;
  const magSquared = (c) => {
    let re = 0;
    let im = 0;
    for (let k = 0; k < 3; k++) {
      re += c[k] * Math.cos(k * w);
      im -= c[k] * Math.sin(k * w);
    }
    return re * re + im * im;
  };
  return 10 * Math.log10(magSquared(b) / magSquared(a));
}

// Loudness-compensation corner frequencies and slope. Two shelves rather
// than one: the ear's low-frequency sensitivity falls away in two stages
// (below ~300 Hz, then again below ~60 Hz), and a single shelf can only fit
// one of them. 0.25 dB of tilt per dB of level drop is what matches the ISO
// 226 contours; the cap stops an extreme cut at very low volumes.
const LOUDNESS_SHELVES = [300, 60];
const LOUDNESS_DB_PER_DB = 0.25;
const LOUDNESS_MAX_DB = 15;

/*
 * Loudness compensation as the device applies it: two high-shelf CUTS, so
 * playing below the reference volume never asks the amplifier for gain it
 * does not have. `dropDb` is how far below the reference anchor the volume
 * sits (a positive number of dB).
 *
 * The visible consequence of the cut form is that below the reference the
 * mids fall about 0.5 dB per dB faster than the slider law alone - the bass
 * is not lifted, everything above it is lowered.
 */
export function loudnessCompensationDb(f, dropDb, fs = DEVICE_SAMPLE_RATE) {
  if (!(dropDb > 0)) return 0;
  const gain = -Math.min(LOUDNESS_MAX_DB, LOUDNESS_DB_PER_DB * dropDb);
  let total = 0;
  for (const fc of LOUDNESS_SHELVES) total += highShelfDb(f, fc, gain, fs);
  return total;
}

/*
 * The same curve read relative to 1 kHz - what the EQ graph draws, because
 * on screen the compensation reads as a bass lift rather than a broadband
 * cut. Within 1.5 dB of the ISO 226:2003 equal-loudness prediction
 * (Lp(f, R-drop) - Lp(f, R) + drop) from 20 Hz to 2 kHz; see the physics
 * test in tests/unit/eq-math.test.js. The fit is deliberately low-frequency
 * only - the contours' treble behaviour depends on absolute SPL, which the
 * device cannot know.
 */
export function loudnessCompensationRelDb(f, dropDb, fs = DEVICE_SAMPLE_RATE) {
  if (!(dropDb > 0)) return 0;
  return loudnessCompensationDb(f, dropDb, fs) - loudnessCompensationDb(1000, dropDb, fs);
}

/*
 * The band gains actually running at a given volume. Between the reference
 * and loud anchors the two curves are crossfaded band-for-band in volume-dB;
 * beyond the loud anchor the loud curve holds, and at or below the reference
 * the reference curve does. Frequencies and Qs are shared, so only the gains
 * move - which is what makes the interpolation meaningful at all.
 */
export function dynamicEqGains(refGains, loudGains, refDb, loudDb, hasLoud, volDb) {
  if (!hasLoud || !Array.isArray(loudGains) || loudDb <= refDb || volDb <= refDb) {
    return refGains.slice();
  }
  const t = clamp((volDb - refDb) / (loudDb - refDb), 0, 1);
  return refGains.map((g, i) => g + t * ((loudGains[i] ?? 0) - g));
}

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

/**
 * Is `stored` the same band list as `wanted`?
 *
 * Used to recognise our own write coming back from the device after a save
 * whose response was lost. The device clamps what it is handed but otherwise
 * stores it verbatim, and fitPeqPoints has already rounded to 0.1 Hz / 0.1 dB
 * / 0.01 Q, so the tolerances only have to absorb float round-tripping - they
 * are deliberately tighter than one rounding step, or a *different* set of
 * bands could pass for ours.
 */
export function peqPointsMatch(stored, wanted) {
  if (!Array.isArray(stored) || !Array.isArray(wanted)) return false;
  if (stored.length !== wanted.length) return false;
  return wanted.every((w, i) => {
    const s = stored[i];
    return !!s &&
      Math.abs(s.freq - w.freq) < 0.05 &&
      Math.abs(s.gain - w.gain) < 0.05 &&
      Math.abs(s.q - w.q) < 0.005;
  });
}
