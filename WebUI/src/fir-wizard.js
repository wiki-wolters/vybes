/*
 * Orchestration logic for the auto-FIR measurement/correction wizard (slice
 * C, docs/AUTO_FIR_CONTRACTS.md). Everything here is a pure function on
 * plain objects and typed arrays - no DOM, no Web Audio, no api-client calls
 * - so it all runs under vitest/node. FirWizardView.vue is the only caller
 * that touches the DOM, the mic, or the network; it exists to sequence
 * these functions against real state.
 *
 * Consumes (does not modify) the slice B math API: sweep-math.js and
 * fir-design.js. The capture-processing pipeline below is written from
 * scratch rather than importing delay-align.js's matched-filter machinery,
 * per the slice C brief - the sweep probe's anchor/drift/segment concerns
 * are different enough (one continuous multi-output multi-pass capture,
 * not a forward/reverse pair) to deserve their own, simpler implementation.
 */

import { fft, nextPow2 } from './fft.js';
import { DEVICE_SAMPLE_RATE } from './device.js';
import {
  generateSweep,
  deconvolve,
  extractSegment,
  estimateDrift,
  resampleByPpm,
  gatedResponse,
  harmonicResponse,
  smoothDb,
} from './sweep-math.js';
import {
  planTapBudget,
  designKernel,
  encodeFirBin,
} from './fir-design.js';
import { targetDbOnFreqs } from './target-curves.js';

// Nominal device sample rate (docs/AUTO_FIR_CONTRACTS.md: "44117.647 Hz
// (nominal 44100 in the probe schedule contract)") - the SWEEP START event
// doesn't echo a rate token (there's nothing to disambiguate; every sample
// count in the schedule is already on this clock), so it's the shared
// constant from device.js, re-exported here - the same role
// PROBE_SCHEDULE.sampleRate plays for the delay probe.
export { DEVICE_SAMPLE_RATE };

export const DEFAULT_FLO_HZ = 20;
export const DEFAULT_FHI_HZ = 20000;
export const MAX_DELAY_US = 20000;

// ===================================================================
// Stage 0 - Plan: passband resolution and tap budgeting
// ===================================================================

/**
 * The frequency an output's hp/lp filter section resolves to, or null when
 * the section is off (the caller substitutes the wide-open default).
 * @param {{mode:string, xover?:string, freq?:number}} filter
 * @param {Array<{id:string, freq:number}>} crossovers
 * @returns {number|null}
 */
export function effectiveFilterFreq(filter, crossovers) {
  if (!filter || filter.mode === 'off') return null;
  if (filter.mode === 'manual') {
    const freq = Number(filter.freq);
    return Number.isFinite(freq) ? freq : null;
  }
  if (filter.mode === 'xover') {
    const point = (crossovers || []).find((c) => c.id === filter.xover);
    return point ? point.freq : null;
  }
  return null;
}

/**
 * Resolve every output's passband from the preset's hp/lp crossover
 * assignments: no hp -> fLo 20, no lp -> fHi 20000 (AUTO_FIR_DESIGN stage 0).
 *
 * @param {{outputs: Array, crossovers?: Array}} preset  a GET /preset shape
 * @returns {Array<{index:number, label:string, enabled:boolean, fLo:number, fHi:number}>}
 */
export function resolvePassbands(preset) {
  const crossovers = preset.crossovers ?? [];
  return preset.outputs.map((o, index) => ({
    index,
    label: o.label,
    enabled: !!o.enabled,
    fLo: effectiveFilterFreq(o.hp, crossovers) ?? DEFAULT_FLO_HZ,
    fHi: effectiveFilterFreq(o.lp, crossovers) ?? DEFAULT_FHI_HZ,
  }));
}

/**
 * Stage 0's tap plan: passbands resolved from the preset, taps allocated by
 * fir-design.js's planTapBudget. Returned rows keep every output (index,
 * label, enabled, band, taps) so the UI can render one table.
 *
 * @param {Object} preset
 * @param {Object} [planOpts]  forwarded to planTapBudget (pool/quantum/reserve/subCutoff)
 * @returns {Array<{index:number, label:string, enabled:boolean, fLo:number, fHi:number, taps:number}>}
 */
export function planWizardTapBudget(preset, planOpts = {}) {
  const passbands = resolvePassbands(preset);
  const plan = planTapBudget(passbands, planOpts);
  const tapsByIndex = new Map(plan.map((p) => [p.index, p.taps]));
  return passbands.map((p) => ({ ...p, taps: tapsByIndex.get(p.index) ?? 0 }));
}

// Extend a passband by a margin (octaves) on each side, clamped to the
// audible range. Used only for the measurement's analysis window (stage
// 2->3, "gatedResponse per output over its passband union correction
// band") - a bit of context past the crossover edge lets the review chart
// show the rolloff shape and gives designKernel's own band taper (which
// reaches a further 1/3 octave past band.fLo/fHi, fir-design.js's
// bandTaper) real data to taper against instead of the log-grid's clamped
// edge value.
const ANALYSIS_MARGIN_OCT = 1;

export function analysisBandFor(passband) {
  return {
    fLo: Math.max(15, passband.fLo / 2 ** ANALYSIS_MARGIN_OCT),
    fHi: Math.min(22000, passband.fHi * 2 ** ANALYSIS_MARGIN_OCT),
  };
}

// ===================================================================
// Sweep schedule / event parsing
// ===================================================================

/**
 * Parse the "START ..." probeEvent line (the SWEEP-prefixed UART reply with
 * its "SWEEP " prefix already stripped by the ESP, same convention as the
 * delay probe's PROBE lines) into the schedule shape sweep-math.js expects
 * plus the enabled-output order for one pass.
 *
 * @param {string} line  e.g. "START 7 2 65536 197222 131072 20.00 20000.00 512"
 * @returns {{type:'start', mask:number, order:number[], schedule: import('./sweep-math.js').SweepSchedule & {nPasses:number}}|null}
 */
export function parseSweepStartLine(line) {
  const parts = String(line).trim().split(/\s+/);
  if (parts[0] !== 'START' || parts.length < 9) return null;
  const mask = Number(parts[1]);
  const nPasses = Number(parts[2]);
  const preRoll = Number(parts[3]);
  const spacing = Number(parts[4]);
  const chirpSamples = Number(parts[5]);
  const f0 = Number(parts[6]);
  const f1 = Number(parts[7]);
  const fade = Number(parts[8]);
  if (![mask, nPasses, preRoll, spacing, chirpSamples, f0, f1, fade].every(Number.isFinite)) {
    return null;
  }
  const order = [];
  for (let ch = 0; ch < 8; ch++) if (mask & (1 << ch)) order.push(ch);
  return {
    type: 'start',
    mask,
    order,
    schedule: { nPasses, preRoll, spacing, chirpSamples, f0, f1, fade, deviceRate: DEVICE_SAMPLE_RATE },
  };
}

/** Parse any sweep probeEvent line into a small tagged object, or null. */
export function parseSweepEventLine(line) {
  const parts = String(line).trim().split(/\s+/);
  switch (parts[0]) {
    case 'START':
      return parseSweepStartLine(line);
    case 'CHIRP':
      return { type: 'chirp', slot: Number(parts[1]), output: Number(parts[2]) };
    case 'WARN':
      return { type: 'warn', reason: parts[1], output: Number(parts[2]) };
    case 'DONE':
      return { type: 'done' };
    case 'STOP':
      return { type: 'stop' };
    case 'ERR':
      return { type: 'err', reason: parts.slice(1).join(' ') };
    default:
      return null;
  }
}

/** Total capture duration the wizard should record, plus network start slack. */
export function sweepDurationS(schedule, nOutputs) {
  const nSlots = nOutputs * schedule.nPasses;
  const samples = schedule.preRoll + nSlots * schedule.spacing;
  return samples / schedule.deviceRate + 3;
}

// ===================================================================
// Stage 2->3 - the capture-processing pipeline
// ===================================================================

// Analytic cross-correlation envelope of `signal` against `ref`, written
// locally rather than imported from delay-align.js (per the slice C brief).
// Same construction as sweep-math.js's private analyticCorrelation and
// delay-align.js's correlationEnvelope: FFT cross-power with the negative
// spectrum zeroed before the inverse, so the envelope is smooth through the
// peak and polarity-blind.
function correlationEnvelope(signal, ref) {
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
    const re = (sRe[i] * rRe[i] + sIm[i] * rIm[i]) * scale;
    const im = (sIm[i] * rRe[i] - sRe[i] * rIm[i]) * scale;
    sRe[i] = re;
    sIm[i] = im;
  }
  fft(sRe, sIm, true);
  const env = new Float64Array(signal.length);
  for (let i = 0; i < env.length; i++) env[i] = Math.hypot(sRe[i], sIm[i]);
  return env;
}

// Sub-sample refinement of an envelope peak in [lo, hi] via a parabola
// through the three samples straddling the integer max.
function refinedPeak(env, lo, hi) {
  lo = Math.max(0, lo);
  hi = Math.min(env.length - 1, hi);
  let peak = lo;
  for (let t = lo + 1; t <= hi; t++) if (env[t] > env[peak]) peak = t;
  let refined = peak;
  if (peak > 0 && peak < env.length - 1) {
    const a = env[peak - 1];
    const b = env[peak];
    const c = env[peak + 1];
    const denom = a - 2 * b + c;
    if (denom < 0) refined = peak + 0.5 * (a - c) / denom;
  }
  return refined;
}

/**
 * Locate slot 0's sweep within the raw capture: matched-filter against the
 * reference sweep, searching only the first schedule interval (slot 0 must
 * arrive before slot 1 possibly could) so a coincidentally louder later
 * slot can never steal the anchor. Returns a sub-sample index into `capture`.
 */
function findSlot0Anchor(capture, ref, schedule, rateRatio) {
  const env = correlationEnvelope(capture, ref);
  const searchEnd = Math.min(
    env.length - ref.length,
    Math.round((schedule.preRoll + schedule.spacing) * rateRatio)
  );
  if (searchEnd < 0) throw new Error('Recording is too short for the sweep schedule');
  return refinedPeak(env, 0, searchEnd);
}

// The matched-filter peak above is the ACOUSTIC arrival of slot 0's output -
// it already contains that output's own electrical+acoustic delay, which
// extractSegment has no way to know about (it slices a fixed window per the
// schedule alone). Anchoring exactly on that peak would put outputs with a
// SMALLER delay than slot 0's at negative time in their own segment, where
// deconvolve's linear/circular split (docs in sweep-math.js: "the wrapped
// negative-time tail... is dropped") silently discards their true response,
// leaving only near-zero regularization leakage - which is exactly the
// large spurious near-zero-index peak that made this pipeline's magnitude
// come out wrong by tens of dB before this margin was added. Backing the
// anchor off by a fixed safety margin - comfortably larger than any
// plausible acoustic+electrical delay (the device's own 20ms delay cap,
// MAX_DELAY_US, plus slack) - shifts every output's TRUE peak later by the
// same constant, keeping it in positive time. The shift is common to every
// output, so relative delays between them (all computeRelativeDelays uses)
// are unaffected.
const ANCHOR_SAFETY_MARGIN_S = 0.03;

// Shift `capture` so index 0 lands on `offsetSamples` (the session's device-
// time-zero instant), zero-filling if that instant precedes the capture.
function trimToAnchor(capture, offsetSamples) {
  const offset = Math.round(offsetSamples);
  if (offset === 0) return capture;
  if (offset > 0) return capture.subarray(Math.min(offset, capture.length));
  const out = new Float32Array(capture.length - offset);
  out.set(capture, -offset);
  return out;
}

// Distortion curves are smooth and each point costs a Goertzel over its
// own window, so they get a coarser grid than the correction measurement.
const HARMONIC_POINTS_PER_OCTAVE = 12;

// Average harmonicResponse results across passes in the power domain, the
// same way averageIrsAcrossPasses treats magnitude. NaN means "this order
// wasn't measurable here" (above Nyquist, or its packet fell outside the
// captured pre-roll) and stays NaN rather than being averaged as if it were
// a zero reading; a point measurable on only some passes averages over the
// passes that had it.
function averageHarmonicsAcrossPasses(passes) {
  if (passes.length === 0) return null;
  if (passes.length === 1) return passes[0];
  const first = passes[0];
  const meanDb = (pick) => {
    const out = new Float64Array(first.freqs.length).fill(NaN);
    for (let i = 0; i < out.length; i++) {
      let sum = 0;
      let n = 0;
      for (const p of passes) {
        const v = pick(p)[i];
        if (Number.isFinite(v)) { sum += Math.pow(10, v / 10); n++; }
      }
      if (n > 0) out[i] = 10 * Math.log10(sum / n);
    }
    return out;
  };
  return {
    freqs: first.freqs,
    orders: first.orders.map((row, o) => ({
      order: row.order,
      relDb: meanDb((p) => p.orders[o].relDb),
    })),
    thdDb: meanDb((p) => p.thdDb),
    floorDb: meanDb((p) => p.floorDb),
  };
}

// Average several equal-length deconvolved IRs into one: magnitude
// averaged in the power domain across passes (reduces noise the way
// signal-averaging normally does), phase taken from the first pass only
// (per docs/AUTO_FIR_CONTRACTS.md's slice C pipeline note) so a
// mid-sequence glitch on a later pass can't rotate the merged phase.
function averageIrsAcrossPasses(irs) {
  if (irs.length === 1) return irs[0];
  const len = irs[0].length;
  const n = nextPow2(len);
  const spectra = irs.map((ir) => {
    const re = new Float64Array(n);
    const im = new Float64Array(n);
    re.set(ir);
    fft(re, im, false);
    return { re, im };
  });
  const outRe = new Float64Array(n);
  const outIm = new Float64Array(n);
  for (let k = 0; k < n; k++) {
    let sumPow = 0;
    for (const s of spectra) sumPow += s.re[k] * s.re[k] + s.im[k] * s.im[k];
    const mag = Math.sqrt(sumPow / spectra.length);
    const phase = Math.atan2(spectra[0].im[k], spectra[0].re[k]);
    outRe[k] = mag * Math.cos(phase);
    outIm[k] = mag * Math.sin(phase);
  }
  fft(outRe, outIm, true);
  const merged = new Float32Array(len);
  for (let i = 0; i < len; i++) merged[i] = outRe[i];
  return merged;
}

// Index of the largest-magnitude sample, searching forward from `from`.
function argmaxAbsFrom(arr, from) {
  let best = from;
  let bv = -1;
  for (let i = from; i < arr.length; i++) {
    const v = Math.abs(arr[i]);
    if (v > bv) { bv = v; best = i; }
  }
  return best;
}

/** Peak-to-background ratio of an IR, in dB - a coarse SNR diagnostic. */
export function irSnrDb(ir, peakIndex, sampleRate) {
  const guard = Math.max(8, Math.round(0.003 * sampleRate));
  const peak = Math.abs(ir[peakIndex] || 0);
  let sumSq = 0;
  let count = 0;
  for (let i = 0; i < ir.length; i++) {
    if (Math.abs(i - peakIndex) <= guard) continue;
    sumSq += ir[i] * ir[i];
    count++;
  }
  const bg = count > 0 ? Math.sqrt(sumSq / count) : 0;
  // Capped rather than Infinity: a real capture always has some analog/
  // quantization noise floor, so an unbounded value here only ever shows up
  // on a degenerate (all-silent) test signal - capping keeps the UI's
  // "SNR NN dB" readout numeric either way.
  return bg > 0 ? Math.min(200, 20 * Math.log10(peak / bg)) : 200;
}

/**
 * Process one whole sweep-probe capture session into a per-output measurement
 * ready for the designer, plus the relative arrival time each output's
 * kernel-independent delay falls out of.
 *
 * Pipeline (docs/AUTO_FIR_CONTRACTS.md, slice C): locate the session anchor
 * from slot 0, trim the capture to it (estimateDrift assumes capture index 0
 * is device-time 0), estimate and correct clock drift, then per output:
 * extractSegment + deconvolve every pass, average across passes (power-domain
 * magnitude, phase from the first pass), gatedResponse over the analysis band.
 *
 * @param {Float32Array} capture   raw mic recording, whole session
 * @param {number} sampleRate      capture rate, Hz
 * @param {{nPasses:number, preRoll:number, spacing:number, chirpSamples:number,
 *          f0:number, f1:number, fade:number, deviceRate:number}} schedule
 * @param {number[]} order         enabled output indices, ascending, one pass's worth
 * @param {Object} [opts]
 * @param {(output:number)=>{fLo:number,fHi:number}} [opts.bandForOutput]
 *   analysis band per output index; defaults to the full audible range
 * @param {number} [opts.pointsPerOctave=24]
 * @param {number} [opts.cycles=8]
 * @returns {{
 *   outputs: Array<{output:number, measurement:{freqs:Float64Array,magDb:Float64Array,excessPhaseRad:Float64Array},
 *                   delayUs:number, snrDb:number, consistencyUs:number}>,
 *   driftPpm:number, driftConfidence:number,
 * }}
 */
export function processSweepCapture(capture, sampleRate, schedule, order, opts = {}) {
  const {
    bandForOutput = () => ({ fLo: DEFAULT_FLO_HZ, fHi: DEFAULT_FHI_HZ }),
    pointsPerOctave = 24,
    cycles = 8,
  } = opts;
  if (order.length === 0) throw new Error('No enabled outputs in the sweep order');

  const rateRatio = sampleRate / schedule.deviceRate;
  const ref = generateSweep(sampleRate, schedule);

  // deviceZeroSample is estimateDrift's required anchor: capture index 0
  // (of whatever's trimmed to it) must be device-time zero, i.e. exactly
  // where the sequencer's preRoll silence began, no safety margin - its own
  // per-slot search window (+/-20ms) already tolerates slot 0's acoustic
  // bias just fine.
  const anchorRaw = findSlot0Anchor(capture, ref, schedule, rateRatio);
  const deviceZeroSample = anchorRaw - schedule.preRoll * rateRatio;
  const trimmedForDrift = trimToAnchor(capture, deviceZeroSample);
  const { ppm, confidence: driftConfidence } = estimateDrift(trimmedForDrift, sampleRate, schedule, order.length);

  // Extraction is different: it has no search tolerance at all (extractSegment
  // slices exactly the computed window), so every slot's window is sliced
  // starting `ANCHOR_SAFETY_MARGIN_S` earlier than its nominal position -
  // trimming the source array itself that much earlier (anchorSeconds stays
  // 0) gives the window the extra leading samples rather than cancelling
  // them back out. Every output's raw peakIndex/delayUs below ends up
  // offset by this same constant margin, which computeRelativeDelays'
  // normalize-to-latest step removes.
  const marginSamples = ANCHOR_SAFETY_MARGIN_S * sampleRate;
  const trimmedForExtraction = trimToAnchor(capture, deviceZeroSample - marginSamples);
  const corrected = schedule.nPasses > 1
    ? resampleByPpm(trimmedForExtraction, -ppm)
    : trimmedForExtraction;

  const sweepSeconds = schedule.chirpSamples / schedule.deviceRate;

  const outputs = order.map((output, pos) => {
    const irs = [];
    const passArrivals = [];
    const passHarmonics = [];
    const band = bandForOutput(output);
    for (let pass = 0; pass < schedule.nPasses; pass++) {
      const slot = pass * order.length + pos;
      const segment = extractSegment(corrected, sampleRate, schedule, slot, 0);
      const { ir, preIr } = deconvolve(segment, ref, sampleRate);
      irs.push(ir);
      passArrivals.push(argmaxAbsFrom(ir, 0));
      // Harmonic packets live at negative time, so they have to be read per
      // pass, before averageIrsAcrossPasses discards everything but the
      // causal side.
      passHarmonics.push(harmonicResponse(ir, preIr, sampleRate, {
        ...band,
        f0: schedule.f0,
        f1: schedule.f1,
        sweepSeconds,
        pointsPerOctave: HARMONIC_POINTS_PER_OCTAVE,
        cycles,
      }));
    }

    const merged = averageIrsAcrossPasses(irs);
    const measurement = gatedResponse(merged, 0, sampleRate, { ...band, pointsPerOctave, cycles });

    const spreadSamples = passArrivals.length > 1
      ? Math.max(...passArrivals) - Math.min(...passArrivals)
      : 0;

    return {
      output,
      measurement,
      // Raw arrival time (samples from each slot's own excitation start,
      // shared zero convention across outputs) - stage 5 turns these into
      // device delayUs via computeRelativeDelays.
      delayUs: (measurement.peakIndex / sampleRate) * 1e6,
      snrDb: irSnrDb(merged, measurement.peakIndex, sampleRate),
      consistencyUs: (spreadSamples / sampleRate) * 1e6,
      harmonics: averageHarmonicsAcrossPasses(passHarmonics),
    };
  });

  return { outputs, driftPpm: ppm, driftConfidence };
}

/**
 * Turn per-output raw arrival times (processSweepCapture's delayUs, a shared
 * but arbitrary zero) into device delaySettings: normalize to the latest
 * arrival, add the delay already configured (the probe measures the chain as
 * currently set up), clamp to the device's range.
 *
 * @param {Array<{output:number, delayUs:number}>} arrivals
 * @param {Record<number, number>} currentDelaysUs  by output index
 * @param {number} [maxDelayUs=MAX_DELAY_US]
 * @returns {Array<{output:number, newDelayUs:number, clamped:boolean}>}
 */
export function computeRelativeDelays(arrivals, currentDelaysUs, maxDelayUs = MAX_DELAY_US) {
  if (arrivals.length === 0) return [];
  const latest = Math.max(...arrivals.map((a) => a.delayUs));
  const raw = arrivals.map((a) => ({
    output: a.output,
    delay: (currentDelaysUs[a.output] || 0) + (latest - a.delayUs),
  }));
  const minDelay = Math.min(...raw.map((r) => r.delay));
  return raw.map((r) => {
    let d = Math.round(r.delay - minDelay);
    let clamped = false;
    if (d > maxDelayUs) { d = maxDelayUs; clamped = true; }
    return { output: r.output, newDelayUs: d, clamped };
  });
}

/**
 * Coarse "does the measured passband match the configured crossover" check
 * (AUTO_FIR_DESIGN stage 3): compares the in-band level against the level
 * just past each configured edge, and flags an edge that isn't rolling off.
 * Only meaningful for edges narrower than the wide-open default.
 *
 * @param {{freqs:Float64Array, magDb:Float64Array}} measurement
 * @param {{fLo:number, fHi:number}} passband
 * @returns {('hp'|'lp')[]|null}
 */
export function passbandContradiction(measurement, passband, opts = {}) {
  const { edgeMarginOct = 0.5, rolloffDb = 6 } = opts;
  const { freqs, magDb } = measurement;
  const medianInRange = (lo, hi) => {
    const vals = [];
    for (let i = 0; i < freqs.length; i++) if (freqs[i] >= lo && freqs[i] <= hi) vals.push(magDb[i]);
    if (!vals.length) return null;
    vals.sort((a, b) => a - b);
    return vals[vals.length >> 1];
  };
  const inBand = medianInRange(passband.fLo, passband.fHi);
  if (inBand == null) return null;

  const problems = [];
  if (passband.fLo > DEFAULT_FLO_HZ * 1.5) {
    const below = medianInRange(passband.fLo / 2 ** (1 + edgeMarginOct), passband.fLo / 2 ** edgeMarginOct);
    if (below != null && inBand - below < rolloffDb) problems.push('hp');
  }
  if (passband.fHi < DEFAULT_FHI_HZ / 1.5) {
    const above = medianInRange(passband.fHi * 2 ** edgeMarginOct, passband.fHi * 2 ** (1 + edgeMarginOct));
    if (above != null && inBand - above < rolloffDb) problems.push('lp');
  }
  return problems.length ? problems : null;
}

/** Below this peak/background ratio a capture is flagged low-SNR in review. */
export const LOW_SNR_DB = 25;

// ===================================================================
// Stage 4 - Design
// ===================================================================

/** The two fixed render presets stage 4 offers, plus a free-form custom slider. */
export const RENDER_PRESETS = [
  { id: 'gaming', label: 'Gaming', latencyBudgetMs: 0 },
  { id: 'music', label: 'Music', latencyBudgetMs: 10 },
];

/** The frequency a given latency budget's phase correction can reach (rate/budget), or null for minimum-phase (no reach limit). */
export function reachHz(latencyBudgetMs, sampleRate) {
  if (!latencyBudgetMs || latencyBudgetMs <= 0) return null;
  const samples = Math.round((latencyBudgetMs / 1000) * sampleRate);
  return samples > 0 ? sampleRate / samples : null;
}

/**
 * The house-curve target, in dB, on one output's measured frequencies.
 *
 * The curve is re-centered over the output's own passband (clipped to what
 * was actually measured) rather than the analyzer's fixed 200-5000 Hz
 * window, which a tweeter or a woofer never overlaps. It need not be exact:
 * designKernel re-centers again on the in-band median of (measurement -
 * target), so only the curve's *shape* inside the band reaches the kernel.
 *
 * @param {{freqs:Float64Array}} measurement
 * @param {{fLo:number, fHi:number}} output
 * @param {Object|null} target  a target-curves.js selection; null = flat
 * @returns {Float64Array|null}
 */
export function targetDbForOutput(measurement, output, target) {
  if (!target) return null;
  const { freqs } = measurement;
  let loHz = Math.max(output.fLo, freqs[0]);
  let hiHz = Math.min(output.fHi, freqs[freqs.length - 1]);
  if (!(hiHz > loHz)) {
    loHz = freqs[0];
    hiHz = freqs[freqs.length - 1];
  }
  return targetDbOnFreqs(target, freqs, { loHz, hiHz });
}

/**
 * Design one output's kernel: smooth the measurement to 1/6 octave, resolve
 * the chosen house curve onto its frequencies, then run fir-design.js's
 * designKernel.
 *
 * @param {{freqs:Float64Array, magDb:Float64Array, excessPhaseRad:Float64Array}} measurement
 * @param {{fLo:number, fHi:number, taps:number}} output   passband + planned taps
 * @param {{sampleRate:number, latencyBudgetMs:number, target?:Object,
 *          smoothFracOctave?:number, presenceCap?:Object,
 *          maxBoostDb?:number, maxCutDb?:number}} opts
 *   `target` is a target-curves.js selection ({mode, tiltDbPerOct, ...});
 *   omitted means a flat target, the pre-target behaviour.
 * @returns {Float32Array}
 */
export function designOutputKernel(measurement, output, opts) {
  const { sampleRate, latencyBudgetMs, target = null, smoothFracOctave = 1 / 6, presenceCap, maxBoostDb, maxCutDb } = opts;
  const smoothedMagDb = smoothDb(measurement.magDb, measurement.freqs, smoothFracOctave);
  const targetDb = targetDbForOutput(measurement, output, target);
  const latencyBudgetSamples = Math.max(0, Math.round((latencyBudgetMs / 1000) * sampleRate));
  return designKernel(
    { freqs: measurement.freqs, magDb: smoothedMagDb, excessPhaseRad: measurement.excessPhaseRad },
    {
      taps: output.taps,
      sampleRate,
      latencyBudgetSamples,
      band: { fLo: output.fLo, fHi: output.fHi },
      ...(targetDb ? { targetDb } : {}),
      ...(presenceCap !== undefined ? { presenceCap } : {}),
      ...(maxBoostDb !== undefined ? { maxBoostDb } : {}),
      ...(maxCutDb !== undefined ? { maxCutDb } : {}),
    }
  );
}

// ===================================================================
// Stage 5 - Apply: naming and the API-call plan
// ===================================================================

/** Turn an output label into the slug half of "a<gen>-<slug>.bin". */
export function slugifyLabel(label) {
  const slug = String(label ?? '')
    .toLowerCase()
    .trim()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '');
  return slug || 'output';
}

const GENERATION_NAME_RE = /^a(\d+)-/;

export function firFilename(generation, slug) {
  return `a${generation}-${slug}.bin`;
}

/**
 * Smallest generation number (>= 1) such that none of `labels`' filenames at
 * that generation already appear in `existingFiles` (GET /fir/files).
 * Bounded so a pathological input can't spin forever.
 *
 * @param {string[]} existingFiles
 * @param {string[]} labels
 * @returns {number}
 */
export function nextGeneration(existingFiles, labels) {
  const existing = new Set(existingFiles);
  const slugs = labels.map(slugifyLabel);
  const MAX_GENERATION = 100000;
  for (let gen = 1; gen <= MAX_GENERATION; gen++) {
    if (slugs.every((slug) => !existing.has(firFilename(gen, slug)))) return gen;
  }
  throw new Error('Could not find a free FIR filename generation');
}

/**
 * Assign one filename per output at the given generation, disambiguating
 * same-label collisions within this batch (e.g. two outputs both labeled
 * "Sub") with a numeric suffix.
 *
 * @param {number} generation
 * @param {Array<{index:number, label:string}>} outputs
 * @returns {Record<number, string>} filename by output index
 */
export function assignFilenames(generation, outputs) {
  const used = new Set();
  const byIndex = {};
  for (const o of outputs) {
    const baseSlug = slugifyLabel(o.label);
    let slug = baseSlug;
    let name = firFilename(generation, slug);
    let suffix = 2;
    while (used.has(name)) {
      slug = `${baseSlug}-${suffix}`;
      name = firFilename(generation, slug);
      suffix++;
    }
    used.add(name);
    byIndex[o.index] = name;
  }
  return byIndex;
}

/**
 * Build the ordered list of API calls that realize the Apply stage, as data
 * rather than executing them - the view dispatches each step to the matching
 * api-client method (`apiClient[step.method](...step.args)`). Order: copy the
 * source preset, upload every kernel, assign each to its output, write the
 * measured relative delays, enable FIR on the copy.
 *
 * @param {Object} args
 * @param {string} args.sourcePresetName
 * @param {string} args.destPresetName
 * @param {string[]} args.existingFirFiles   GET /fir/files result
 * @param {Array<{index:number, label:string, kernel:Float32Array, delayUs?:number}>} args.outputs
 * @returns {Array<{method:string, args:Array<*>}>}
 */
export function buildApplyPlan({ sourcePresetName, destPresetName, existingFirFiles, outputs }) {
  const labels = outputs.map((o) => o.label);
  const generation = nextGeneration(existingFirFiles, labels);
  const filenames = assignFilenames(generation, outputs);

  const steps = [{ method: 'copyPreset', args: [sourcePresetName, destPresetName] }];
  for (const o of outputs) {
    steps.push({ method: 'uploadFir', args: [filenames[o.index], encodeFirBin(o.kernel)] });
  }
  for (const o of outputs) {
    steps.push({ method: 'setOutputFir', args: [destPresetName, o.index, filenames[o.index]] });
  }
  let wroteDelay = false;
  for (const o of outputs) {
    if (o.delayUs != null) {
      steps.push({ method: 'setOutputDelay', args: [destPresetName, o.index, Math.round(o.delayUs)] });
      wroteDelay = true;
    }
  }
  // Matches api-client.js's method names (updateFIREnabled,
  // setSpeakerDelayEnabled), so the view can dispatch every step as
  // `apiClient[step.method](...step.args)`. The copy inherits whatever
  // delaysEnabled the source had - without turning it on here, freshly
  // written delays would sit inert if the source had it off (the common
  // case: measuring relative delays for the first time was the point of
  // running this wizard).
  if (wroteDelay) {
    steps.push({ method: 'setSpeakerDelayEnabled', args: [destPresetName, true] });
  }
  steps.push({ method: 'updateFIREnabled', args: [destPresetName, true] });
  return steps;
}
