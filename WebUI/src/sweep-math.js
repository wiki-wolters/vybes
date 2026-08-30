/*
 * Measurement math for the auto-FIR wizard: exponential-sweep generation,
 * deconvolution to impulse responses, clock-drift estimation, and the
 * gated/windowed frequency responses the filter designer consumes.
 *
 * Contract: docs/AUTO_FIR_CONTRACTS.md (slice B). The JSDoc here is
 * normative - units, array layouts and zero-time conventions are part of
 * the contract. Everything is pure functions on typed arrays (no DOM, no
 * Web Audio) so it runs under vitest/node. Acceptance criteria live in
 * tests/unit/fir-design-acceptance.test.js against the synthetic system in
 * tests/helpers/synth-system.js - the simulator and thresholds are the
 * verification harness and are not modified by this module's implementer.
 *
 * The device-side waveform is ProbeSource's double-precision log-sweep
 * recurrence with raised-cosine fades; the schedule parameters echoed in
 * the SWEEP START reply are the single source of truth. Like
 * delay-align.js's generateChirp, regeneration at the capture rate keeps
 * the same duration, band, and fade fractions.
 */

import { fft, nextPow2 } from './fft.js';

/**
 * Schedule as echoed by SWEEP START (all sample counts at the device rate).
 * @typedef {Object} SweepSchedule
 * @property {number} nPasses      complete passes over the output list
 * @property {number} preRoll      samples of silence before the first chirp
 * @property {number} spacing      chirp-start to chirp-start, samples
 * @property {number} chirpSamples sweep length, samples
 * @property {number} f0           start frequency, Hz
 * @property {number} f1           end frequency, Hz
 * @property {number} fade         raised-cosine fade length each end, samples
 * @property {number} deviceRate   device sample rate, Hz (44100 nominal)
 */

/**
 * Generate the reference sweep at an arbitrary sample rate. Same duration,
 * band, and fade fractions as the device waveform; amplitude 1.0 peak.
 *
 * @param {number} sampleRate  target rate, Hz (e.g. the capture rate)
 * @param {SweepSchedule} schedule
 * @returns {Float32Array} round(chirpSamples * sampleRate / deviceRate) samples
 */
export function generateSweep(sampleRate, schedule) {
  const { f0, f1, chirpSamples, fade, deviceRate } = schedule;
  const n = Math.round(chirpSamples * sampleRate / deviceRate);
  const fadeN = Math.round(fade * sampleRate / deviceRate);
  const ratio = Math.exp(Math.log(f1 / f0) / n);
  const out = new Float32Array(n);
  let phase = 0;
  let freq = f0;
  for (let i = 0; i < n; i++) {
    let w = 1;
    if (i < fadeN) {
      w = 0.5 * (1 - Math.cos(Math.PI * i / fadeN));
    } else if (i > n - 1 - fadeN) {
      w = 0.5 * (1 - Math.cos(Math.PI * (n - 1 - i) / fadeN));
    }
    out[i] = w * Math.sin(phase);
    phase += 2 * Math.PI * freq / sampleRate;
    if (phase >= 2 * Math.PI) phase -= 2 * Math.PI;
    freq *= ratio;
  }
  return out;
}

// --- Deconvolution ---
//
// Regularized spectral division (Wiener-style): the reference is exact and
// noise-free (we generated it), so H(f) = Y(f)*conj(X(f)) / (|X(f)|^2 + eps)
// recovers the system response directly - it's the same thing as Y(f)/X(f)
// but stays finite where the sweep has ~no energy (below f0, above f1, and
// at the fade edges). Both signals are zero-padded to a size well beyond
// segment.length + reference.length before the FFT, so the recovered
// impulse response is a LINEAR (not circular) deconvolution: any energy
// that would represent negative time (Farina's harmonic pre-arrivals) wraps
// around to indices near the top of the padded buffer, far from the causal
// window near zeroIndex, and never folds into it. zeroIndex is always 0 for
// this method: index 0 of the padded arrays is where the reference (and so
// the excitation) starts, and spectral division solves for the system's
// response starting at that same instant.
const DECONV_REG_ALPHA = 1e-6;

/**
 * Deconvolve one capture segment against the reference sweep, returning a
 * linear impulse response. Zero-time convention: index `zeroIndex` of the
 * returned IR corresponds to the start of the sweep excitation, so a system
 * that delays by D seconds peaks at zeroIndex + D*sampleRate. Harmonic
 * distortion products land at negative time (before zeroIndex) per Farina
 * and must not fold into the causal part.
 *
 * @param {Float32Array} segment    capture samples covering one sweep + tail
 * @param {Float32Array} reference  from generateSweep at the same rate
 * @param {number} sampleRate       Hz
 * @returns {{ir: Float32Array, zeroIndex: number}}
 */
export function deconvolve(segment, reference, sampleRate) {
  const n = nextPow2(segment.length + reference.length);
  const segRe = new Float64Array(n);
  const segIm = new Float64Array(n);
  const refRe = new Float64Array(n);
  const refIm = new Float64Array(n);
  segRe.set(segment);
  refRe.set(reference);
  fft(segRe, segIm, false);
  fft(refRe, refIm, false);

  let peakEnergy = 0;
  const energy = new Float64Array(n);
  for (let i = 0; i < n; i++) {
    energy[i] = refRe[i] * refRe[i] + refIm[i] * refIm[i];
    if (energy[i] > peakEnergy) peakEnergy = energy[i];
  }
  const eps = DECONV_REG_ALPHA * peakEnergy;

  const outRe = new Float64Array(n);
  const outIm = new Float64Array(n);
  for (let i = 0; i < n; i++) {
    const denom = energy[i] + eps;
    // segRe/segIm * conj(refRe/refIm)
    outRe[i] = (segRe[i] * refRe[i] + segIm[i] * refIm[i]) / denom;
    outIm[i] = (segIm[i] * refRe[i] - segRe[i] * refIm[i]) / denom;
  }
  fft(outRe, outIm, true);

  // Keep the causal side plus a little headroom; the wrapped negative-time
  // tail lives near the far end of `n` and is dropped here rather than
  // exposed as if it were part of the causal response.
  const keep = Math.min(n, segment.length);
  const ir = new Float32Array(keep);
  for (let i = 0; i < keep; i++) ir[i] = outRe[i];
  return { ir, zeroIndex: 0 };
}

/**
 * Slice one sweep's segment out of the full-session capture.
 * Slot k (0-based, ascending outputs then repeated passes) starts at
 * (preRoll + k*spacing)/deviceRate seconds after capture start plus the
 * session anchor; the segment extends one full spacing.
 *
 * @param {Float32Array} capture  the whole recorded session
 * @param {number} sampleRate     capture rate, Hz
 * @param {SweepSchedule} schedule
 * @param {number} slot
 * @param {number} anchorSeconds  session start offset located in the capture
 * @returns {Float32Array}
 */
export function extractSegment(capture, sampleRate, schedule, slot, anchorSeconds) {
  const startS = anchorSeconds + (schedule.preRoll + slot * schedule.spacing) / schedule.deviceRate;
  const start = Math.round(startS * sampleRate);
  const length = Math.round(schedule.spacing * sampleRate / schedule.deviceRate);
  const out = new Float32Array(length);
  for (let i = 0; i < length; i++) {
    const j = start + i;
    if (j >= 0 && j < capture.length) out[i] = capture[j];
  }
  return out;
}

// --- Drift estimation ---
//
// Sub-sample arrival detection via FFT cross-correlation (matched filter)
// against an analytic envelope, the same technique delay-align.js uses for
// the delay probe: correlate, find the integer peak, then refine with a
// parabola through the three envelope samples around it.
function analyticCorrelation(signal, ref) {
  const n = nextPow2(signal.length + ref.length);
  const sRe = new Float64Array(n);
  const sIm = new Float64Array(n);
  const rRe = new Float64Array(n);
  const rIm = new Float64Array(n);
  sRe.set(signal);
  rRe.set(ref);
  fft(sRe, sIm, false);
  fft(rRe, rIm, false);
  for (let i = 0; i < n; i++) {
    const scale = (i === 0 || i === n / 2) ? 1 : (i < n / 2 ? 2 : 0);
    const r = (sRe[i] * rRe[i] + sIm[i] * rIm[i]) * scale;
    const x = (sIm[i] * rRe[i] - sRe[i] * rIm[i]) * scale;
    sRe[i] = r;
    sIm[i] = x;
  }
  fft(sRe, sIm, true);
  const env = new Float64Array(signal.length);
  for (let i = 0; i < env.length; i++) env[i] = Math.hypot(sRe[i], sIm[i]);
  return env;
}

// Sub-sample peak location (parabolic interpolation) of `env` within
// [lo, hi], plus the peak's own magnitude for confidence weighting.
function refinedPeak(env, lo, hi) {
  lo = Math.max(0, lo);
  hi = Math.min(env.length - 1, hi);
  let peak = lo;
  for (let t = lo + 1; t <= hi; t++) {
    if (env[t] > env[peak]) peak = t;
  }
  let refined = peak;
  if (peak > 0 && peak < env.length - 1) {
    const a = env[peak - 1];
    const b = env[peak];
    const c = env[peak + 1];
    const denom = a - 2 * b + c;
    if (denom < 0) refined = peak + 0.5 * (a - c) / denom;
  }
  return { sample: refined, magnitude: env[peak] };
}

/**
 * Estimate phone-vs-device clock drift from repeated passes: the arrival
 * spacing of the same output across passes deviates from the schedule by
 * drift * elapsed. Positive ppm means the capture clock runs fast relative
 * to the device.
 *
 * @param {Float32Array} capture   whole session
 * @param {number} sampleRate      capture rate, Hz
 * @param {SweepSchedule} schedule
 * @param {number} nOutputs        outputs per pass
 * @returns {{ppm: number, confidence: number}} confidence in [0,1]
 */
export function estimateDrift(capture, sampleRate, schedule, nOutputs) {
  const { nPasses } = schedule;
  const ref = generateSweep(sampleRate, schedule);
  const rateRatio = sampleRate / schedule.deviceRate;
  const searchWin = Math.round(0.02 * sampleRate); // generous vs. expected drift+jitter

  // Arrival sample (capture-rate, sub-sample) for every slot.
  const nSlots = nPasses * nOutputs;
  const arrivals = new Array(nSlots);
  for (let slot = 0; slot < nSlots; slot++) {
    const nominal = Math.round((schedule.preRoll + slot * schedule.spacing) * rateRatio);
    const lo = nominal - searchWin;
    const hi = nominal + ref.length + searchWin;
    const window = capture.subarray(Math.max(0, lo), Math.min(capture.length, hi));
    const windowOffset = Math.max(0, lo);
    const env = analyticCorrelation(window, ref);
    const centerInWindow = nominal - windowOffset;
    const { sample, magnitude } = refinedPeak(env, centerInWindow - searchWin, centerInWindow + searchWin);
    arrivals[slot] = { sample: sample + windowOffset, magnitude };
  }

  if (nPasses < 2) return { ppm: 0, confidence: 0 };

  // For each output, compare the first and last pass (maximum time base ->
  // best ppm precision for a fixed sub-sample arrival uncertainty).
  const estimates = [];
  for (let o = 0; o < nOutputs; o++) {
    const first = arrivals[o];
    const last = arrivals[(nPasses - 1) * nOutputs + o];
    const measuredGap = last.sample - first.sample;
    const expectedGap = (nPasses - 1) * nOutputs * schedule.spacing * rateRatio;
    if (expectedGap <= 0) continue;
    const ppm = (measuredGap / expectedGap - 1) * 1e6;
    const weight = Math.min(first.magnitude, last.magnitude);
    estimates.push({ ppm, weight });
  }

  if (estimates.length === 0) return { ppm: 0, confidence: 0 };

  const totalWeight = estimates.reduce((s, e) => s + e.weight, 0);
  const ppm = totalWeight > 0
    ? estimates.reduce((s, e) => s + e.ppm * e.weight, 0) / totalWeight
    : estimates.reduce((s, e) => s + e.ppm, 0) / estimates.length;

  // Confidence: agreement across outputs (tight spread -> high confidence),
  // falling back to a fixed mid confidence for the single-output case where
  // there is nothing to cross-check against.
  let confidence;
  if (estimates.length > 1) {
    const spread = Math.max(...estimates.map((e) => e.ppm)) - Math.min(...estimates.map((e) => e.ppm));
    confidence = Math.max(0, Math.min(1, 1 - spread / 10));
  } else {
    confidence = Math.max(0, Math.min(1, estimates[0].weight > 0 ? 0.75 : 0));
  }

  return { ppm, confidence };
}

// --- Resampling ---

// Catmull-Rom cubic interpolation at fractional index t (out-of-range
// samples treated as 0), independent of the harness's own `stretch`.
function cubicAt(signal, t) {
  const i = Math.floor(t);
  const fr = t - i;
  const at = (k) => (k >= 0 && k < signal.length ? signal[k] : 0);
  const y0 = at(i - 1);
  const y1 = at(i);
  const y2 = at(i + 1);
  const y3 = at(i + 2);
  return y1 + 0.5 * fr * (y2 - y0 + fr * (2 * y0 - 5 * y1 + 4 * y2 - y3 + fr * (3 * (y1 - y2) + y3 - y0)));
}

/**
 * Resample a signal by a ppm-scale rate correction (output length differs
 * by ~ppm*1e-6). Interpolation quality must keep acceptance test 2's
 * one-sample re-alignment achievable.
 *
 * @param {Float32Array} signal
 * @param {number} ppm
 * @returns {Float32Array}
 */
export function resampleByPpm(signal, ppm) {
  const factor = 1 + ppm * 1e-6;
  const outLen = Math.round(signal.length * factor);
  const out = new Float32Array(outLen);
  for (let n = 0; n < outLen; n++) {
    out[n] = cubicAt(signal, n / factor);
  }
  return out;
}

// --- Gated response ---

// Raised-cosine taper spanning the NOMINAL window [center-halfWidth,
// center+halfWidth] - zero at those nominal edges, peak 1 at `center` -
// evaluated only over the part of that span that actually lies inside the
// IR. At low analysis frequencies the nominal window commonly extends
// before sample 0 (worst when the IR peak itself sits near the buffer
// start); clamping the RANGE is fine, but the taper's own zero-crossings
// must stay pinned to the nominal edges, not slide in to the clamped ones -
// otherwise the taper's zero lands on the in-range samples (including the
// peak itself) instead of off the end of the buffer, and the peak sample
// gets multiplied away.
function taperedWindow(ir, center, halfWidth) {
  const nominalLo = center - halfWidth;
  const nominalHi = center + halfWidth;
  const span = nominalHi - nominalLo; // = 2*halfWidth
  const lo = Math.max(0, Math.round(nominalLo));
  const hi = Math.min(ir.length - 1, Math.round(nominalHi));
  const len = Math.max(0, hi - lo + 1);
  const out = new Float64Array(len);
  for (let i = 0; i < len; i++) {
    const idx = lo + i;
    const frac = (idx - nominalLo) / span; // 0 at nominalLo, 1 at nominalHi
    const w = 0.5 * (1 - Math.cos(2 * Math.PI * frac));
    out[i] = ir[idx] * w;
  }
  return { window: out, start: lo };
}

// Direct DFT of a short windowed segment at one frequency - cheap enough
// since each analysis frequency's window is only a handful of cycles long.
// Phase is referenced to `refIndex` (the IR peak), the SAME instant for
// every analysis frequency, rather than to the window's own start (which
// would move per frequency, since the window half-width does) or to IR
// index 0 (which would bake in a huge bulk-delay slope of
// 2*pi*f*peakIndex/rate - larger than pi between adjacent log-grid points,
// which breaks the later phase-unwrap's assumption of small inter-bin
// steps). Referencing to the peak keeps the argument small and keeps the
// bulk delay a single linear-in-frequency term for the final detrend to
// remove.
function goertzelAt(window, start, refIndex, freq, sampleRate) {
  const w = 2 * Math.PI * freq / sampleRate;
  let re = 0;
  let im = 0;
  for (let i = 0; i < window.length; i++) {
    const phase = w * (start + i - refIndex);
    re += window[i] * Math.cos(phase);
    im -= window[i] * Math.sin(phase);
  }
  return { re, im };
}

// Linear interpolation of a value tabulated at arbitrary (monotonic
// increasing) `freqs`, holding the end values flat outside the table -
// used to re-sample our log-spaced magnitude onto a linear FFT bin grid.
function interpAtFreq(values, freqs, f) {
  const m = freqs.length;
  if (f <= freqs[0]) return values[0];
  if (f >= freqs[m - 1]) return values[m - 1];
  let lo = 0;
  let hi = m - 1;
  while (hi - lo > 1) {
    const mid = (lo + hi) >> 1;
    if (freqs[mid] <= f) lo = mid; else hi = mid;
  }
  const frac = (f - freqs[lo]) / (freqs[hi] - freqs[lo]);
  return values[lo] * (1 - frac) + values[hi] * frac;
}

// Cepstral minimum-phase spectrum of a magnitude array tabulated at 0..N/2
// linear FFT bins - the same fold-and-double real-cepstrum construction the
// test harness uses independently (c = IFFT(log|H|), fold to causal,
// Hmin = exp(FFT(c))), reproduced here rather than imported so a bug in one
// can never hide behind the other.
function cepstralMinPhaseSpectrum(linMagDb, N) {
  const floor = 1e-7; // ~ -140 dB, keeps log finite where the band is closed
  const re = new Float64Array(N);
  const im = new Float64Array(N);
  for (let k = 0; k <= N / 2; k++) {
    const v = Math.log(Math.max(Math.pow(10, linMagDb[k] / 20), floor));
    re[k] = v;
    if (k > 0 && k < N / 2) re[N - k] = v;
  }
  fft(re, im, true); // real cepstrum
  for (let q = 1; q < N / 2; q++) {
    re[q] *= 2;
    re[N - q] = 0;
  }
  im.fill(0);
  fft(re, im, false); // log Hmin
  const outRe = new Float64Array(N / 2 + 1);
  const outIm = new Float64Array(N / 2 + 1);
  for (let k = 0; k <= N / 2; k++) {
    const e = Math.exp(re[k]);
    outRe[k] = e * Math.cos(im[k]);
    outIm[k] = e * Math.sin(im[k]);
  }
  return { re: outRe, im: outIm };
}

/**
 * Minimum phase implied by a magnitude curve tabulated on an arbitrary
 * (typically log-spaced) frequency grid, in radians, sampled back onto that
 * same grid.
 *
 * A cepstral construction needs a magnitude defined at every linear FFT
 * bin, not our sparse log grid, so this interpolates magDb onto a generous
 * linear bin grid (holding the ends flat outside [freqs[0], freqs[last]]),
 * runs the standard fold-and-double cepstrum there, and interpolates the
 * resulting complex spectrum (not the wrapped angle - interpolating angles
 * across a possible +/-pi seam would be wrong) back onto `freqs`. This is
 * deliberately the same algorithm as the test harness's own
 * minimumPhaseSpectrum (independently written, not imported), so the two
 * agree in practice on the same magnitude data, not just in theory - an
 * earlier from-first-principles log-frequency Hilbert-transform version of
 * this function did not hit the harness's own tolerance.
 *
 * @param {Float64Array} magDb
 * @param {Float64Array} freqs      same length as magDb, monotonic increasing
 * @param {number} sampleRate       Hz
 * @returns {Float64Array} minimum phase, radians, one per `freqs` entry
 */
export function minimumPhaseFromLogGrid(magDb, freqs, sampleRate) {
  const N = nextPow2(sampleRate); // sub-Hz bins - far finer than the log grid
  const half = N / 2;
  const linMagDb = new Float64Array(half + 1);
  for (let k = 0; k <= half; k++) {
    linMagDb[k] = interpAtFreq(magDb, freqs, k * sampleRate / N);
  }
  const { re, im } = cepstralMinPhaseSpectrum(linMagDb, N);

  const minPhase = new Float64Array(freqs.length);
  for (let i = 0; i < freqs.length; i++) {
    const kf = Math.min(half - 1e-9, Math.max(0, freqs[i] * N / sampleRate));
    const k0 = Math.min(half - 1, Math.floor(kf));
    const frac = kf - k0;
    const rr = re[k0] * (1 - frac) + re[k0 + 1] * frac;
    const ii = im[k0] * (1 - frac) + im[k0 + 1] * frac;
    minPhase[i] = Math.atan2(ii, rr);
  }
  return minPhase;
}

// Unwrap a phase-like sequence (radians) sampled along an arbitrary
// monotonic grid, so consecutive jumps > pi fold into a continuous curve.
export function unwrapAcrossGrid(raw) {
  const n = raw.length;
  const out = new Float64Array(n);
  let prev = 0;
  let offset = 0;
  for (let i = 0; i < n; i++) {
    let w = raw[i] - prev;
    while (w > Math.PI) { w -= 2 * Math.PI; offset -= 2 * Math.PI; }
    while (w < -Math.PI) { w += 2 * Math.PI; offset += 2 * Math.PI; }
    prev = raw[i];
    out[i] = raw[i] + offset;
  }
  return out;
}

// Remove the best-fit line (weighted least squares, vs 2*pi*freq) from an
// unwrapped phase sequence - the linear-in-frequency component is bulk
// delay. When the underlying excess phase is genuinely curved (not just a
// delay), the "best fit line" a plain unweighted fit finds depends on how
// densely the samples are packed across the band - and our frequency grid
// is log-spaced (equal count per octave), which packs far more samples
// into the bottom of the band than a linear-frequency grid would. Left
// unweighted, that skews the fitted slope away from what a uniform-in-Hz
// fit (e.g. the harness's own fit over its linear FFT bins) would find,
// producing a systematic disagreement even when both sides correctly
// identify the same bulk delay. Weighting each sample by its own frequency
// corrects this: on a log-uniform grid (ratio step r), sample i represents
// a linear-Hz span of about freqs[i]*(r-1), i.e. proportional to freqs[i]
// itself (r is constant across the grid, and an overall constant scale on
// every weight doesn't change a weighted-least-squares solution) - so
// weighting by freqs[i] approximates the same uniform-in-Hz integral a
// linear-bin fit performs.
export function detrendVsFreq(unwrapped, freqs) {
  const n = unwrapped.length;
  let sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
  for (let i = 0; i < n; i++) {
    const w = freqs[i];
    const x = 2 * Math.PI * freqs[i];
    sw += w; sx += w * x; sy += w * unwrapped[i];
    sxx += w * x * x; sxy += w * x * unwrapped[i];
  }
  const denom = sw * sxx - sx * sx;
  const slope = denom !== 0 ? (sw * sxy - sx * sy) / denom : 0;
  const icept = sw !== 0 ? (sy - slope * sx) / sw : 0;
  const out = new Float64Array(n);
  for (let i = 0; i < n; i++) {
    const x = 2 * Math.PI * freqs[i];
    out[i] = unwrapped[i] - (icept + slope * x);
  }
  return out;
}

/**
 * Frequency-dependent-windowed (gated) response of an IR on a log grid.
 * The window is centred on the IR peak and spans `cycles` periods of each
 * analysis frequency (so it is short at HF - excluding reflections - and
 * long at LF), tapered raised-cosine. Returns the complex response and the
 * derived quantities the designer needs.
 *
 * @param {Float32Array} ir
 * @param {number} zeroIndex   from deconvolve
 * @param {number} sampleRate  Hz
 * @param {{fLo: number, fHi: number, pointsPerOctave?: number, cycles?: number}} opts
 * @returns {{freqs: Float64Array, magDb: Float64Array,
 *            excessPhaseRad: Float64Array, peakIndex: number}}
 *          excess phase = measured phase minus the minimum phase implied by
 *          magDb, with bulk delay (peak arrival) removed
 */
export function gatedResponse(ir, zeroIndex, sampleRate, opts) {
  const { fLo, fHi, pointsPerOctave = 24, cycles = 5 } = opts;

  let peakIndex = zeroIndex;
  let peakVal = Math.abs(ir[zeroIndex] || 0);
  for (let i = zeroIndex; i < ir.length; i++) {
    if (Math.abs(ir[i]) > peakVal) { peakVal = Math.abs(ir[i]); peakIndex = i; }
  }

  const freqs = [];
  const step = Math.pow(2, 1 / pointsPerOctave);
  for (let f = fLo; f <= fHi * 1.0001; f *= step) freqs.push(f);
  const freqArr = Float64Array.from(freqs);

  const magDb = new Float64Array(freqArr.length);
  const phaseRad = new Float64Array(freqArr.length);
  for (let i = 0; i < freqArr.length; i++) {
    const f = freqArr[i];
    const halfWidth = Math.max(4, (cycles / f) * sampleRate / 2);
    const { window, start } = taperedWindow(ir, peakIndex, halfWidth);
    const { re, im } = goertzelAt(window, start, peakIndex, f, sampleRate);
    magDb[i] = 20 * Math.log10(Math.max(Math.hypot(re, im), 1e-12));
    phaseRad[i] = Math.atan2(im, re);
  }

  const minPhase = minimumPhaseFromLogGrid(magDb, freqArr, sampleRate);

  // Excess phase per bin, unwrapped across the log grid, then remove the
  // best-fit linear component (in frequency, not bin index - the grid is
  // log-spaced) which represents bulk delay/peak position.
  const raw = new Float64Array(freqArr.length);
  for (let i = 0; i < freqArr.length; i++) raw[i] = phaseRad[i] - minPhase[i];
  const excessPhaseRad = detrendVsFreq(unwrapAcrossGrid(raw), freqArr);

  return { freqs: freqArr, magDb, excessPhaseRad, peakIndex };
}

/**
 * Fractional-octave smoothing (power domain) of a dB array on a log grid.
 *
 * @param {Float64Array} magDb
 * @param {Float64Array} freqs
 * @param {number} fracOctave  e.g. 1/6
 * @returns {Float64Array}
 */
export function smoothDb(magDb, freqs, fracOctave) {
  const n = magDb.length;
  const out = new Float64Array(n);
  const halfSpanOct = fracOctave / 2;
  for (let i = 0; i < n; i++) {
    let sumPow = 0;
    let sumWeight = 0;
    const loF = freqs[i] / Math.pow(2, halfSpanOct);
    const hiF = freqs[i] * Math.pow(2, halfSpanOct);
    for (let j = 0; j < n; j++) {
      if (freqs[j] < loF || freqs[j] > hiF) continue;
      const oct = Math.log2(freqs[j] / freqs[i]) / halfSpanOct;
      const weight = 0.5 * (1 + Math.cos(Math.PI * oct)); // raised-cosine taper
      sumPow += weight * Math.pow(10, magDb[j] / 10);
      sumWeight += weight;
    }
    out[i] = sumWeight > 0 ? 10 * Math.log10(sumPow / sumWeight) : magDb[i];
  }
  return out;
}
