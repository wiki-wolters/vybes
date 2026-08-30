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
 * @param {Array<{index: number, enabled: boolean, fLo: number, fHi: number}>} outputs
 *   passband edges per output, already resolved from hp/lp crossover
 *   assignments by the caller (fLo=20 when no high-pass, fHi=20000 when no
 *   low-pass)
 * @param {{poolTotal?: number, quantum?: number, reserveQuanta?: number,
 *          subCutoffHz?: number}} [opts]  defaults: FIR_TAP_POOL, 128, 2, 120
 * @returns {Array<{index: number, taps: number}>} disabled outputs get 0
 */
export function planTapBudget(outputs, opts = {}) {
  throw new Error('not implemented - see docs/AUTO_FIR_CONTRACTS.md');
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
  throw new Error('not implemented - see docs/AUTO_FIR_CONTRACTS.md');
}

/**
 * Predicted result of applying a kernel to the measured system - the
 * before/after the wizard previews, and what acceptance tests check against
 * their own independent FFT.
 *
 * @param {Float32Array} kernel
 * @param {{freqs: Float64Array, magDb: Float64Array,
 *          excessPhaseRad: Float64Array}} measurement
 * @param {number} sampleRate
 * @returns {{freqs: Float64Array, magDb: Float64Array,
 *            excessPhaseRad: Float64Array}}
 */
export function predictCorrected(kernel, measurement, sampleRate) {
  throw new Error('not implemented - see docs/AUTO_FIR_CONTRACTS.md');
}

/**
 * Encode a kernel as the device's .bin format: raw little-endian float32
 * taps, no header (loader: FMT_BIN; ESP tap accounting: size/4).
 *
 * @param {Float32Array} kernel
 * @returns {ArrayBuffer} kernel.length * 4 bytes
 */
export function encodeFirBin(kernel) {
  throw new Error('not implemented - see docs/AUTO_FIR_CONTRACTS.md');
}
