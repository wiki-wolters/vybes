// Acceptance suite for slice B of the auto-FIR work - the numbered criteria
// in docs/AUTO_FIR_CONTRACTS.md, verified against the synthetic systems in
// tests/helpers/synth-system.js with that harness's own independent
// FFT/DFT machinery.
//
// The suites are skipped while src/sweep-math.js and src/fir-design.js are
// stubs; removing the `.skip`s is part of slice B's delivery. The harness
// and the thresholds here are the contract - implementations conform to
// them, not the other way around.
import { describe, it, expect } from 'vitest';
import {
  makeSystemIr,
  convolve,
  freqResponse,
  logSpace,
  stretch,
  simulateCapture,
  excessPhaseOf,
  measurementOf,
  rms,
  argmaxAbs,
  makeRng,
} from '../helpers/synth-system.js';
import {
  generateSweep,
  deconvolve,
  estimateDrift,
  resampleByPpm,
  gatedResponse,
} from '../../src/sweep-math.js';
import {
  planTapBudget,
  designKernel,
  predictCorrected,
  encodeFirBin,
  FIR_TAP_POOL,
  FIR_POOL_QUANTUM,
} from '../../src/fir-design.js';
import { targetDbOnFreqs } from '../../src/target-curves.js';

const RATE = 44100;

// Schedule shared by the measurement criteria (test-sized: a short sweep
// keeps the suite fast without changing any of the math being verified).
const SCHEDULE = {
  nPasses: 2,
  preRoll: 16384,
  spacing: 131072,
  chirpSamples: 65536,
  f0: 20,
  f1: 20000,
  fade: 512,
  deviceRate: RATE,
};

// The design criteria all use this driver-like plant: passband wider than
// the correction band so band-edge behavior is the kernel's, not the
// plant's.
const PLANT_BAND = { lo: 100, hi: 16000 };
const CORR_BAND = { fLo: 300, fHi: 8000 };
const DESIGN_GRID = { ...CORR_BAND, pointsPerOctave: 24 };

describe('criterion 1: deconvolution recovers a known delay', () => {
  it('peaks at 5 ms +/- 0.1 ms, >= 40 dB above background', () => {
    const system = makeSystemIr(RATE, { delayS: 0.005, band: { lo: 20, hi: 20000 } });
    const ref = generateSweep(RATE, SCHEDULE);
    const segment = simulateCapture(system, ref, {
      sampleRate: RATE,
      noiseDbFs: -80,
      seed: 11,
    });
    const { ir, zeroIndex } = deconvolve(segment, ref, RATE);
    const peak = argmaxAbs(ir);
    const delayMs = ((peak - zeroIndex) / RATE) * 1000;
    expect(Math.abs(delayMs - 5)).toBeLessThanOrEqual(0.1);

    // Background: RMS over the causal 40 ms window, excluding +/- 3 ms
    // around the peak.
    const guard = Math.round(0.003 * RATE);
    const bg = [];
    for (let i = zeroIndex; i < Math.min(ir.length, zeroIndex + Math.round(0.04 * RATE)); i++) {
      if (Math.abs(i - peak) > guard) bg.push(ir[i]);
    }
    const ratioDb = 20 * Math.log10(Math.abs(ir[peak]) / rms(Float64Array.from(bg)));
    expect(ratioDb).toBeGreaterThanOrEqual(40);
  });
});

describe('criterion 2: clock drift is estimated and correctable', () => {
  // One output, two passes: build the ideal session at the device rate,
  // stretch the whole thing by +80 ppm (fast capture clock), add noise.
  function buildSession(driftPpm) {
    const system = makeSystemIr(RATE, { delayS: 0.004, band: { lo: 20, hi: 20000 } });
    const ref = generateSweep(RATE, SCHEDULE);
    const wet = convolve(ref, system);
    const len = SCHEDULE.preRoll + SCHEDULE.spacing * 2 + wet.length;
    let session = new Float32Array(len);
    for (let slot = 0; slot < 2; slot++) {
      const at = SCHEDULE.preRoll + slot * SCHEDULE.spacing;
      for (let i = 0; i < wet.length; i++) session[at + i] += wet[i];
    }
    session = stretch(session, 1 + driftPpm * 1e-6);
    const rng = makeRng(23);
    const amp = Math.pow(10, -80 / 20) * Math.sqrt(3);
    for (let i = 0; i < session.length; i++) session[i] += amp * (2 * rng() - 1);
    return { session, ref };
  }

  it('recovers +80 ppm within +/- 5 ppm and re-aligns to within a sample', () => {
    const { session, ref } = buildSession(80);
    const { ppm } = estimateDrift(session, RATE, SCHEDULE, 1);
    expect(Math.abs(ppm - 80)).toBeLessThanOrEqual(5);

    // After correcting by the estimate, the two passes' arrivals must sit
    // one schedule spacing apart to within a sample (measured by the
    // harness's own cross-correlation).
    const fixed = resampleByPpm(session, -ppm);
    const corr = convolve(fixed, Float32Array.from(ref).reverse());
    const half = SCHEDULE.preRoll + SCHEDULE.spacing / 2;
    const first = argmaxAbs(corr.subarray(0, half + ref.length));
    const second = half + ref.length + argmaxAbs(corr.subarray(half + ref.length));
    expect(Math.abs(second - first - SCHEDULE.spacing)).toBeLessThanOrEqual(1);
  });
});

describe('criteria 3-6: kernel design', () => {
  const designOpts = (taps, latencyBudgetSamples, extra = {}) => ({
    taps,
    sampleRate: RATE,
    latencyBudgetSamples,
    band: CORR_BAND,
    ...extra,
  });

  it('criterion 3: flattens magnitude in-band, stays unity out-of-band', () => {
    const system = makeSystemIr(RATE, {
      band: PLANT_BAND,
      resonances: [
        { freq: 400, q: 2, gainDb: 6 },
        { freq: 2500, q: 3, gainDb: -4 },
      ],
    });
    const kernel = designKernel(measurementOf(system, RATE, DESIGN_GRID), designOpts(2048, 0));
    const corrected = convolve(kernel, system);
    const inBand = freqResponse(corrected, RATE, logSpace(300, 8000, 24)).magDb;
    expect(Math.max(...inBand) - Math.min(...inBand)).toBeLessThanOrEqual(2);

    const outBand = freqResponse(kernel, RATE, Float64Array.from([50, 100, 12000, 15000])).magDb;
    for (const db of outBand) expect(Math.abs(db)).toBeLessThanOrEqual(1.5);
  });

  it('criterion 3b: a tilted house target is tracked as tightly as flat', () => {
    const system = makeSystemIr(RATE, {
      band: PLANT_BAND,
      resonances: [
        { freq: 400, q: 2, gainDb: 6 },
        { freq: 2500, q: 3, gainDb: -4 },
      ],
    });
    const measurement = measurementOf(system, RATE, DESIGN_GRID);
    const targetDb = targetDbOnFreqs({ mode: 'tilt', tiltDbPerOct: -1 }, measurement.freqs, {
      loHz: CORR_BAND.fLo,
      hiHz: CORR_BAND.fHi,
    });
    const kernel = designKernel(measurement, designOpts(2048, 0, { targetDb }));

    // Criterion 3's flatness test, measured against the target instead of a
    // flat line: the residual (corrected - target) spans no more than 2 dB.
    const freqs = logSpace(300, 8000, 24);
    const corrected = freqResponse(convolve(kernel, system), RATE, freqs).magDb;
    const want = targetDbOnFreqs({ mode: 'tilt', tiltDbPerOct: -1 }, freqs, {
      loHz: CORR_BAND.fLo,
      hiHz: CORR_BAND.fHi,
    });
    const residual = corrected.map((db, i) => db - want[i]);
    expect(Math.max(...residual) - Math.min(...residual)).toBeLessThanOrEqual(2);

    // And it really is tilted: the kernel pulls ~1 dB/octave of slope out of
    // the (flat-target-wise) identical plant.
    const flatKernel = designKernel(measurement, designOpts(2048, 0));
    const octave = [1000, 2000];
    const tiltedAt = freqResponse(kernel, RATE, Float64Array.from(octave)).magDb;
    const flatAt = freqResponse(flatKernel, RATE, Float64Array.from(octave)).magDb;
    const slope = (tiltedAt[1] - tiltedAt[0]) - (flatAt[1] - flatAt[0]);
    expect(slope).toBeCloseTo(-1, 0);
  });

  it('criterion 4: budget 0 renders minimum-phase', () => {
    const system = makeSystemIr(RATE, {
      band: PLANT_BAND,
      resonances: [{ freq: 400, q: 2, gainDb: 6 }],
    });
    const kernel = designKernel(measurementOf(system, RATE, DESIGN_GRID), designOpts(2048, 0));

    let total = 0;
    let head = 0;
    for (let i = 0; i < kernel.length; i++) {
      total += kernel[i] * kernel[i];
      if (i < 64) head += kernel[i] * kernel[i];
    }
    expect(head / total).toBeGreaterThanOrEqual(0.5);

    // Group delay of the corrected system <= 1 ms across the band
    // (finite differences on a dense linear grid keep the phase unwrapped).
    const corrected = convolve(kernel, system);
    const freqs = [];
    for (let f = 300; f <= 8000; f += 25) freqs.push(f);
    const { phaseRad } = freqResponse(corrected, RATE, Float64Array.from(freqs));
    for (let i = 1; i < freqs.length; i++) {
      let dphi = phaseRad[i] - phaseRad[i - 1];
      while (dphi > Math.PI) dphi -= 2 * Math.PI;
      while (dphi < -Math.PI) dphi += 2 * Math.PI;
      const gd = -dphi / (2 * Math.PI * 25);
      expect(Math.abs(gd)).toBeLessThanOrEqual(0.001);
    }
  });

  it('criterion 5: latency budget buys excess-phase correction', () => {
    const system = makeSystemIr(RATE, {
      band: PLANT_BAND,
      allpasses: [{ freq: 1000, q: 1 }],
    });
    const budget = 441; // 10 ms
    const kernel = designKernel(measurementOf(system, RATE, DESIGN_GRID), designOpts(2048, budget));

    // Bulk lead is exactly the budget: the corrected system's peak lands
    // budget samples after the plant's.
    const corrected = convolve(kernel, system);
    expect(Math.abs(argmaxAbs(corrected) - argmaxAbs(system) - budget)).toBeLessThanOrEqual(2);

    // Excess phase reduced >= 70% over the band the budget reaches.
    const freqs = logSpace(300, 8000, 24);
    const before = rms(excessPhaseOf(system, RATE, freqs));
    const after = rms(excessPhaseOf(corrected, RATE, freqs));
    expect(after).toBeLessThanOrEqual(0.3 * before);
  });

  it('criterion 6: the presence cap limits correction in 1-5 kHz', () => {
    const system = makeSystemIr(RATE, {
      band: PLANT_BAND,
      resonances: [{ freq: 2000, q: 2, gainDb: 6 }],
    });
    const kernel = designKernel(
      measurementOf(system, RATE, DESIGN_GRID),
      designOpts(2048, 0, { presenceCap: { fLo: 1000, fHi: 5000, db: 3 } })
    );
    const at2k = freqResponse(kernel, RATE, Float64Array.from([2000])).magDb[0];
    expect(at2k).toBeGreaterThanOrEqual(-3.3); // never more than the cap
    expect(at2k).toBeLessThanOrEqual(-2.0); // but still correcting toward it
  });
});

describe('criterion 7: tap budget', () => {
  // The design doc's example: 3-way stereo + 2 subs.
  const outputs = [
    { index: 0, enabled: true, fLo: 2500, fHi: 20000 }, // tweeter L
    { index: 1, enabled: true, fLo: 2500, fHi: 20000 }, // tweeter R
    { index: 2, enabled: true, fLo: 300, fHi: 2500 }, // mid L
    { index: 3, enabled: true, fLo: 300, fHi: 2500 }, // mid R
    { index: 4, enabled: true, fLo: 80, fHi: 300 }, // woofer L
    { index: 5, enabled: true, fLo: 80, fHi: 300 }, // woofer R
    { index: 6, enabled: true, fLo: 20, fHi: 80 }, // sub 1
    { index: 7, enabled: false, fLo: 20, fHi: 80 }, // sub 2 (disabled)
  ];

  it('allocates by passband, zeroes subs, quantizes, and leaves reserve', () => {
    const plan = planTapBudget(outputs);
    const byIndex = new Map(plan.map((p) => [p.index, p.taps]));

    expect(byIndex.get(6)).toBe(0); // sub: below the cutoff
    expect(byIndex.get(7)).toBe(0); // disabled

    for (const { taps } of plan) expect(taps % FIR_POOL_QUANTUM).toBe(0);
    for (const i of [0, 1, 2, 3, 4, 5]) {
      expect(byIndex.get(i)).toBeGreaterThanOrEqual(256);
    }

    // Symmetric passbands get identical budgets; lower fLo means more taps.
    expect(byIndex.get(0)).toBe(byIndex.get(1));
    expect(byIndex.get(2)).toBe(byIndex.get(3));
    expect(byIndex.get(4)).toBe(byIndex.get(5));
    expect(byIndex.get(4)).toBeGreaterThan(byIndex.get(2));
    expect(byIndex.get(2)).toBeGreaterThan(byIndex.get(0));
    // Proportionality to 1/fLo within quantization slack: 300/80 = 3.75.
    expect(byIndex.get(4) / byIndex.get(2)).toBeGreaterThanOrEqual(2);
    expect(byIndex.get(4) / byIndex.get(2)).toBeLessThanOrEqual(4.5);

    // Fills the budget without exceeding pool minus reserve.
    const total = plan.reduce((s, p) => s + p.taps, 0);
    const budget = FIR_TAP_POOL - 2 * FIR_POOL_QUANTUM;
    expect(total).toBeLessThanOrEqual(budget);
    expect(total).toBeGreaterThan(budget - 6 * FIR_POOL_QUANTUM);
  });
});

describe('criterion 8: .bin encoding round-trips', () => {
  it('is raw little-endian float32', () => {
    const kernel = Float32Array.from([0.5, -0.25, 1, 3.5e-5]);
    const buf = encodeFirBin(kernel);
    expect(buf.byteLength).toBe(16);
    const view = new DataView(buf);
    for (let i = 0; i < kernel.length; i++) {
      expect(view.getFloat32(i * 4, true)).toBe(kernel[i]);
    }
  });
});

describe('criterion 9: gated response fidelity', () => {
  // gatedResponse is what the wizard feeds the designer from real captures,
  // so it must agree with the harness's ungated view on reflection-free
  // systems - including when the IR peak sits close to the buffer start,
  // where the analysis window necessarily extends before time zero.
  const GATE_OPTS = { fLo: 300, fHi: 8000, pointsPerOctave: 24, cycles: 8 };
  const PLANT = {
    band: { lo: 100, hi: 16000 },
    resonances: [{ freq: 700, q: 2, gainDb: 4 }],
  };

  function magMatch(ir) {
    const gated = gatedResponse(ir, 0, RATE, GATE_OPTS);
    const truth = freqResponse(ir, RATE, gated.freqs).magDb;
    // Both curves have arbitrary absolute level conventions; compare shapes.
    let gMean = 0;
    let tMean = 0;
    for (let i = 0; i < gated.freqs.length; i++) {
      gMean += gated.magDb[i];
      tMean += truth[i];
    }
    gMean /= gated.freqs.length;
    tMean /= gated.freqs.length;
    let worst = 0;
    for (let i = 0; i < gated.freqs.length; i++) {
      worst = Math.max(worst, Math.abs(gated.magDb[i] - gMean - (truth[i] - tMean)));
    }
    return { gated, worstDb: worst };
  }

  it('matches the ungated magnitude within 1.5 dB, peak near buffer start', () => {
    const ir = makeSystemIr(RATE, PLANT); // delayS 0: worst case for the gate
    expect(magMatch(ir).worstDb).toBeLessThanOrEqual(1.5);
  });

  it('matches the ungated magnitude within 1.5 dB with a realistic delay', () => {
    const ir = makeSystemIr(RATE, { ...PLANT, delayS: 0.005 });
    expect(magMatch(ir).worstDb).toBeLessThanOrEqual(1.5);
  });

  it('reads ~zero excess phase on a minimum-phase system', () => {
    const ir = makeSystemIr(RATE, { ...PLANT, delayS: 0.005 });
    const gated = gatedResponse(ir, 0, RATE, GATE_OPTS);
    expect(rms(gated.excessPhaseRad)).toBeLessThanOrEqual(0.2);
  });

  it('agrees with the harness on an allpass system', () => {
    const ir = makeSystemIr(RATE, {
      band: { lo: 100, hi: 16000 },
      allpasses: [{ freq: 1000, q: 1 }],
      delayS: 0.005,
    });
    const gated = gatedResponse(ir, 0, RATE, GATE_OPTS);
    expect(rms(gated.excessPhaseRad)).toBeGreaterThanOrEqual(0.5);
    const truth = excessPhaseOf(ir, RATE, gated.freqs);
    const diff = new Float64Array(gated.freqs.length);
    for (let i = 0; i < diff.length; i++) diff[i] = gated.excessPhaseRad[i] - truth[i];
    expect(rms(diff)).toBeLessThanOrEqual(0.3);
  });

  it('excludes a late reflection at high frequencies - the point of gating', () => {
    const clean = makeSystemIr(RATE, { band: { lo: 100, hi: 16000 }, delayS: 0.005 });
    const reflected = makeSystemIr(RATE, {
      band: { lo: 100, hi: 16000 },
      delayS: 0.005,
      reflection: { delayS: 0.006, gain: 0.4 },
    });
    const gc = gatedResponse(clean, 0, RATE, GATE_OPTS);
    const gr = gatedResponse(reflected, 0, RATE, GATE_OPTS);
    // Ungated, the comb is plainly visible up high...
    const hf = logSpace(2000, 8000, 24);
    const combed = freqResponse(reflected, RATE, hf).magDb;
    expect(Math.max(...combed) - Math.min(...combed)).toBeGreaterThanOrEqual(4);
    // ...gated, the HF curve barely knows the reflection exists.
    for (let i = 0; i < gc.freqs.length; i++) {
      if (gc.freqs[i] < 2000) continue;
      expect(Math.abs(gr.magDb[i] - gc.magDb[i])).toBeLessThanOrEqual(1);
    }
  });
});

describe('criterion 10: predictCorrected invariances', () => {
  it('a delta kernel changes nothing', () => {
    const system = makeSystemIr(RATE, {
      band: PLANT_BAND,
      allpasses: [{ freq: 1000, q: 1 }],
    });
    const measurement = measurementOf(system, RATE, DESIGN_GRID);
    const delta = new Float32Array(256);
    delta[0] = 1;
    const out = predictCorrected(delta, measurement, RATE);
    for (let i = 0; i < measurement.freqs.length; i++) {
      expect(out.magDb[i]).toBeCloseTo(measurement.magDb[i], 1);
    }
    const diff = new Float64Array(measurement.freqs.length);
    for (let i = 0; i < diff.length; i++) {
      diff[i] = out.excessPhaseRad[i] - measurement.excessPhaseRad[i];
    }
    expect(rms(diff)).toBeLessThanOrEqual(0.05);
  });

  it('a pure-delay kernel is bulk delay, not excess phase or magnitude', () => {
    const system = makeSystemIr(RATE, { band: PLANT_BAND });
    const measurement = measurementOf(system, RATE, DESIGN_GRID);
    const shifted = new Float32Array(1024);
    shifted[441] = 1;
    const out = predictCorrected(shifted, measurement, RATE);
    for (let i = 0; i < measurement.freqs.length; i++) {
      expect(out.magDb[i]).toBeCloseTo(measurement.magDb[i], 1);
    }
    expect(rms(out.excessPhaseRad)).toBeLessThanOrEqual(
      rms(measurement.excessPhaseRad) + 0.05
    );
  });
});
