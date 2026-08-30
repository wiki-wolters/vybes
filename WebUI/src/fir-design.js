/*
 * Correction-kernel design for the auto-FIR wizard: tap budgeting from the
 * preset topology, inverse-filter design from a gated measurement, and the
 * .bin encoding the device loads.
 *
 * Contract: docs/AUTO_FIR_CONTRACTS.md (slice B); the JSDoc here is
 * normative. Acceptance: tests/unit/fir-design-acceptance.test.js. Design
 * rationale (division of labor, latency budget, gating): the "Filter
 * design" section of docs/AUTO_FIR_DESIGN.md.
 *
 * Key invariants, restated from the contract:
 *  - Every kernel of a render carries the identical bulk lead
 *    (latencyBudgetSamples), so inter-output time alignment survives
 *    correction. Budget 0 means a minimum-phase kernel.
 *  - Correction is confined to [band.fLo, band.fHi]; the kernel is unity
 *    (0 dB, linear phase) outside, with smooth transitions.
 *  - Boost/cut caps are applied to the kernel's own response, after
 *    smoothing, before phase assignment.
 */

import { minimumPhaseFromLogGrid, unwrapAcrossGrid, detrendVsFreq } from './sweep-math.js';
import { fft, nextPow2 } from './fft.js';

/** Pool geometry (mirrors ESP config.h / teensy_protocol.h). */
export const FIR_TAP_POOL = 12288;
export const FIR_POOL_QUANTUM = 128;

/**
 * Plan the per-output tap allocation from the preset topology.
 * Rule (AUTO_FIR_DESIGN): taps proportional to 1/fLo of each output's
 * passband, quantized up to the 128-tap charge quantum, subs (fHi below
 * `subCutoffHz`) get zero, and at least `reserveQuanta` partitions of the
 * pool stay free.
 *
 * Algorithm: find the largest common support factor k such that
 * Sum(quantizeUp(clamp(k/fLo, 256, 8192))) fits the budget - each term is
 * non-decreasing in k, so the sum is too, and bisection finds the largest
 * feasible k directly (no need for the device sample rate here: k folds
 * rate and the proportionality constant together into one search variable).
 *
 * @param {Array<{index: number, enabled: boolean, fLo: number, fHi: number}>} outputs
 *   passband edges per output, already resolved from hp/lp crossover
 *   assignments by the caller (fLo=20 when no high-pass, fHi=20000 when no
 *   low-pass)
 * @param {{poolTotal?: number, quantum?: number, reserveQuanta?: number,
 *          subCutoffHz?: number}} [opts]  defaults: FIR_TAP_POOL, 128, 2, 120
 * @returns {Array<{index: number, taps: number}>} disabled outputs get 0
 */
export function planTapBudget(outputs, opts = {}) {
  const {
    poolTotal = FIR_TAP_POOL,
    quantum = FIR_POOL_QUANTUM,
    reserveQuanta = 2,
    subCutoffHz = 120,
  } = opts;
  const budget = poolTotal - reserveQuanta * quantum;
  const MIN_TAPS = 256;
  const MAX_TAPS = 8192;

  const isCorrected = (o) => o.enabled && o.fHi > subCutoffHz;
  const eligible = outputs.filter(isCorrected);

  const tapsFor = (k, fLo) => {
    const clamped = Math.min(MAX_TAPS, Math.max(MIN_TAPS, k / fLo));
    return Math.ceil(clamped / quantum) * quantum;
  };
  const sumFor = (k) => eligible.reduce((s, o) => s + tapsFor(k, o.fLo), 0);

  let bestK = 0;
  if (eligible.length > 0) {
    const maxFLo = Math.max(...eligible.map((o) => o.fLo));
    let lo = 0;
    let hi = MAX_TAPS * maxFLo * 1.01; // saturates every eligible output
    if (sumFor(hi) <= budget) {
      bestK = hi;
    } else {
      for (let i = 0; i < 100; i++) {
        const mid = (lo + hi) / 2;
        if (sumFor(mid) <= budget) lo = mid; else hi = mid;
      }
      bestK = lo;
    }
  }

  return outputs.map((o) => ({
    index: o.index,
    taps: isCorrected(o) ? tapsFor(bestK, o.fLo) : 0,
  }));
}

/**
 * Design one output's correction kernel from its gated measurement.
 *
 * @param {{freqs: Float64Array, magDb: Float64Array,
 *          excessPhaseRad: Float64Array}} measurement
 *   from sweep-math gatedResponse (already smoothed to taste by the caller)
 * @param {Object} opts
 * @param {number} opts.taps                 kernel length, multiple of 128
 * @param {number} opts.sampleRate           device rate, Hz
 * @param {number} opts.latencyBudgetSamples bulk lead; 0 = minimum phase
 * @param {{fLo: number, fHi: number}} opts.band  correction band
 * @param {Float64Array} [opts.targetDb]     target magnitude on measurement's
 *                                           grid; omitted = flat
 * @param {number} [opts.maxBoostDb=6]       kernel gain ceiling in-band
 * @param {number} [opts.maxCutDb=12]        kernel cut floor in-band
 * @param {{fLo: number, fHi: number, db: number}} [opts.presenceCap]
 *   tighter symmetric cap inside this band (default 1000-5000 Hz, 3 dB)
 * @returns {Float32Array} the kernel, length opts.taps
 */
export function designKernel(measurement, opts) {
  const { freqs, magDb, excessPhaseRad } = measurement;
  const {
    taps,
    sampleRate,
    latencyBudgetSamples,
    band,
    targetDb = null,
    maxBoostDb = 6,
    maxCutDb = 12,
    presenceCap = { fLo: 1000, fHi: 5000, db: 3 },
  } = opts;

  // Reference level: the in-band median of (measurement - target), so
  // "corrected to target" means the level the output already plays at, not
  // an absolute one - the measurement's absolute level is mic gain and
  // distance, which the kernel must not try to undo.
  const deviations = [];
  for (let i = 0; i < freqs.length; i++) {
    if (freqs[i] >= band.fLo && freqs[i] <= band.fHi) {
      deviations.push(magDb[i] - (targetDb ? targetDb[i] : 0));
    }
  }
  deviations.sort((a, b) => a - b);
  const ref = deviations.length ? deviations[(deviations.length - 1) >> 1] : 0;

  const corrOnGrid = new Float64Array(freqs.length);
  for (let i = 0; i < freqs.length; i++) {
    corrOnGrid[i] = (targetDb ? targetDb[i] : 0) + ref - magDb[i];
  }

  // Per-bin desired kernel gain and excess-phase correction on a linear
  // grid fine enough that the log-grid interpolation, not the bin spacing,
  // sets the resolution.
  const N = Math.max(nextPow2(taps * 4), 8192);
  const half = N / 2;
  const gainDb = new Float64Array(half + 1);
  const excCorr = new Float64Array(half + 1);
  // Phase lead is bought with latency, and the purchase reaches down to
  // roughly one period of the budget: zero correction below that frequency,
  // full correction an octave above it.
  const reach = latencyBudgetSamples > 0 ? sampleRate / latencyBudgetSamples : Infinity;
  for (let k = 1; k <= half; k++) {
    const f = (k * sampleRate) / N;
    const taper = bandTaper(f, band.fLo, band.fHi);
    if (taper === 0) continue;
    let c = interpLogFreqClamped(corrOnGrid, freqs, f) * taper;
    c = Math.min(maxBoostDb, Math.max(-maxCutDb, c));
    if (presenceCap && f >= presenceCap.fLo && f <= presenceCap.fHi) {
      c = Math.min(presenceCap.db, Math.max(-presenceCap.db, c));
    }
    gainDb[k] = c;
    if (latencyBudgetSamples > 0 && f > reach) {
      const r = f >= 2 * reach ? 1 : 0.5 * (1 - Math.cos((Math.PI * Math.log(f / reach)) / Math.log(2)));
      excCorr[k] = -interpLogFreqClamped(excessPhaseRad, freqs, f) * taper * r;
    }
  }

  // Magnitude -> minimum phase, then the excess-phase correction rotates in
  // on top; the result is Hermitian by construction.
  const magLin = new Float64Array(half + 1);
  for (let k = 0; k <= half; k++) magLin[k] = Math.pow(10, gainDb[k] / 20);
  const mp = cepstralMinPhase(magLin, N);

  const re = new Float64Array(N);
  const im = new Float64Array(N);
  for (let k = 0; k <= half; k++) {
    const c = Math.cos(excCorr[k]);
    const s = Math.sin(excCorr[k]);
    re[k] = mp.re[k] * c - mp.im[k] * s;
    im[k] = mp.re[k] * s + mp.im[k] * c;
    if (k > 0 && k < half) {
      re[N - k] = re[k];
      im[N - k] = -im[k];
    }
  }
  im[0] = 0;
  im[half] = 0;
  fft(re, im, true);

  // The ideal response is zero-centred (acausal support = the phase
  // correction's lookahead, which the reach ramp confines to ~budget
  // samples); delaying by the budget realises it causally, and every kernel
  // of a render shares that same bulk lead. Truncate to `taps` with a tail
  // fade so the cut edge doesn't ring.
  const kernel = new Float32Array(taps);
  const B = latencyBudgetSamples;
  const fadeLen = Math.max(16, taps >> 3);
  for (let n = 0; n < taps; n++) {
    let v = re[(((n - B) % N) + N) % N];
    const fromEnd = taps - 1 - n;
    if (fromEnd < fadeLen) v *= 0.5 * (1 - Math.cos((Math.PI * fromEnd) / fadeLen));
    kernel[n] = v;
  }
  return kernel;
}

// --- designKernel internals ---

// Linear interpolation in log-frequency, clamped to the grid's edge values
// outside its range (the band taper is what takes over out there).
function interpLogFreqClamped(values, freqs, f) {
  const last = freqs.length - 1;
  if (f <= freqs[0]) return values[0];
  if (f >= freqs[last]) return values[last];
  let lo = 0;
  let hi = last;
  while (hi - lo > 1) {
    const mid = (lo + hi) >> 1;
    if (freqs[mid] <= f) lo = mid;
    else hi = mid;
  }
  const t = Math.log(f / freqs[lo]) / Math.log(freqs[hi] / freqs[lo]);
  return values[lo] + t * (values[hi] - values[lo]);
}

// Raised-cosine confinement taper: 1 across [fLo, fHi], falling to 0 over
// one transition width OUTSIDE each band edge (correction stays full
// strength to the very edge of the band - the fitter's band limits already
// chose where correction is trusted).
function bandTaper(f, fLo, fHi, edgeOct = 1 / 3) {
  const e = Math.pow(2, edgeOct);
  if (f >= fLo && f <= fHi) return 1;
  if (f <= fLo / e || f >= fHi * e) return 0;
  const t = f < fLo ? Math.log((f * e) / fLo) / Math.log(e) : Math.log((fHi * e) / f) / Math.log(e);
  return 0.5 * (1 - Math.cos(Math.PI * t));
}

// Minimum-phase complex spectrum for a magnitude on linear FFT bins
// (0..N/2), via the real cepstrum. Same construction as the test harness
// uses - the acceptance checks that matter (corrected-system flatness and
// group delay) are measured with a direct DFT, independent of this.
function cepstralMinPhase(magLin, N) {
  const floor = 1e-6;
  const half = N / 2;
  const re = new Float64Array(N);
  const im = new Float64Array(N);
  for (let k = 0; k <= half; k++) {
    const v = Math.log(Math.max(magLin[k], floor));
    re[k] = v;
    if (k > 0 && k < half) re[N - k] = v;
  }
  fft(re, im, true);
  for (let q = 1; q < half; q++) {
    re[q] *= 2;
    re[N - q] = 0;
  }
  im.fill(0);
  fft(re, im, false);
  const outRe = new Float64Array(half + 1);
  const outIm = new Float64Array(half + 1);
  for (let k = 0; k <= half; k++) {
    const mag = Math.exp(re[k]);
    outRe[k] = mag * Math.cos(im[k]);
    outIm[k] = mag * Math.sin(im[k]);
  }
  return { re: outRe, im: outIm };
}

/**
 * Predicted result of applying a kernel to the measured system - the
 * before/after the wizard previews, and what acceptance tests check against
 * their own independent FFT.
 *
 * Convolution multiplies complex responses, so magnitudes add in dB.
 * Excess phase is trickier: `measurement.excessPhaseRad` already has the
 * original system's own bulk delay and minimum phase stripped out via
 * whatever convention produced it, and must be left exactly as it is when
 * the kernel contributes none of its own (a delta kernel is the identity).
 * The kernel is a concrete finite array whose *true* phase we can compute
 * directly (no gating ambiguity) - but a kernel carrying a large bulk lead
 * (a latency budget, or literally a pure delay) has a phase-vs-frequency
 * slope that outruns the pi-per-step budget the later unwrap needs, exactly
 * the failure mode gatedResponse hit before its own peak-referencing fix.
 * So the DFT here is referenced to the kernel's own dominant tap (its
 * argmax), keeping the raw phase argument small; only the kernel's own
 * excess (relative to the minimum phase implied by its own magnitude) is
 * unwrapped and detrended - on its own, never mixed into a second detrend
 * of the measurement's excess phase - and added to the measurement's
 * excess phase unchanged.
 *
 * @param {Float32Array} kernel
 * @param {{freqs: Float64Array, magDb: Float64Array,
 *          excessPhaseRad: Float64Array}} measurement
 * @param {number} sampleRate
 * @returns {{freqs: Float64Array, magDb: Float64Array,
 *            excessPhaseRad: Float64Array}}
 */
export function predictCorrected(kernel, measurement, sampleRate) {
  const { freqs, magDb, excessPhaseRad } = measurement;
  const n = freqs.length;

  let refIndex = 0;
  let refVal = Math.abs(kernel[0] || 0);
  for (let t = 1; t < kernel.length; t++) {
    if (Math.abs(kernel[t]) > refVal) { refVal = Math.abs(kernel[t]); refIndex = t; }
  }

  const kernelMagDb = new Float64Array(n);
  const kernelPhase = new Float64Array(n);
  for (let i = 0; i < n; i++) {
    const w = 2 * Math.PI * freqs[i] / sampleRate;
    let re = 0;
    let im = 0;
    for (let t = 0; t < kernel.length; t++) {
      const phase = w * (t - refIndex);
      re += kernel[t] * Math.cos(phase);
      im -= kernel[t] * Math.sin(phase);
    }
    kernelMagDb[i] = 20 * Math.log10(Math.max(Math.hypot(re, im), 1e-12));
    kernelPhase[i] = Math.atan2(im, re);
  }
  const kernelMinPhase = minimumPhaseFromLogGrid(kernelMagDb, freqs, sampleRate);

  const correctedMagDb = new Float64Array(n);
  const kernelRawExcess = new Float64Array(n);
  for (let i = 0; i < n; i++) {
    correctedMagDb[i] = magDb[i] + kernelMagDb[i];
    kernelRawExcess[i] = kernelPhase[i] - kernelMinPhase[i];
  }
  const kernelExcess = detrendVsFreq(unwrapAcrossGrid(kernelRawExcess), freqs);

  const correctedExcessPhaseRad = new Float64Array(n);
  for (let i = 0; i < n; i++) correctedExcessPhaseRad[i] = excessPhaseRad[i] + kernelExcess[i];

  return { freqs, magDb: correctedMagDb, excessPhaseRad: correctedExcessPhaseRad };
}

/**
 * Encode a kernel as the device's .bin format: raw little-endian float32
 * taps, no header (loader: FMT_BIN; ESP tap accounting: size/4).
 *
 * @param {Float32Array} kernel
 * @returns {ArrayBuffer} kernel.length * 4 bytes
 */
export function encodeFirBin(kernel) {
  const buf = new ArrayBuffer(kernel.length * 4);
  const view = new DataView(buf);
  for (let i = 0; i < kernel.length; i++) {
    view.setFloat32(i * 4, kernel[i], true);
  }
  return buf;
}
