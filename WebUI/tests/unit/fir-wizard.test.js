// Tests for the auto-FIR wizard's orchestration logic (slice C,
// docs/AUTO_FIR_CONTRACTS.md). Passband resolution and naming are pure data
// transforms; the end-to-end test builds a synthetic capture with the
// slice B verification harness (tests/helpers/synth-system.js, imported
// read-only) and checks the whole stage 2->3 pipeline recovers the known
// system shape and inter-output delay from it.
import { describe, it, expect } from 'vitest';
import {
  makeSystemIr,
  convolve,
  stretch,
  makeRng,
  freqResponse,
  logSpace,
  measurementOf,
} from '../helpers/synth-system.js';
import { generateSweep } from '../../src/sweep-math.js';
import {
  resolvePassbands,
  planWizardTapBudget,
  slugifyLabel,
  nextGeneration,
  assignFilenames,
  buildApplyPlan,
  parseSweepStartLine,
  parseSweepEventLine,
  processSweepCapture,
  computeRelativeDelays,
  reachHz,
  designOutputKernel,
  targetDbForOutput,
} from '../../src/fir-wizard.js';

const RATE = 44100;

// A realistic preset shape (mirrors mock-server/templates.js's '3way-2sub'):
// two crossover points, some outputs referencing them, some manual filters,
// one output with neither hp nor lp configured, and a disabled output.
function makePreset() {
  return {
    crossovers: [
      { id: 'sub_xo', freq: 80, type: 'LR4', locked: false, min: 40, max: 500 },
      { id: 'twt_xo', freq: 2500, type: 'LR4', locked: true, min: 800, max: 8000 },
    ],
    outputs: [
      { label: 'L Low', enabled: true, hp: { mode: 'xover', xover: 'sub_xo' }, lp: { mode: 'xover', xover: 'twt_xo' } },
      { label: 'R Low', enabled: true, hp: { mode: 'xover', xover: 'sub_xo' }, lp: { mode: 'xover', xover: 'twt_xo' } },
      { label: 'L High', enabled: true, hp: { mode: 'xover', xover: 'twt_xo' }, lp: { mode: 'off' } },
      { label: 'R High', enabled: true, hp: { mode: 'manual', freq: 3000, type: 'LR4' }, lp: { mode: 'off' } },
      { label: 'Full Range', enabled: true, hp: { mode: 'off' }, lp: { mode: 'off' } },
      { label: 'Sub 1', enabled: true, hp: { mode: 'off' }, lp: { mode: 'xover', xover: 'sub_xo' } },
      { label: 'Sub 2', enabled: false, hp: { mode: 'off' }, lp: { mode: 'xover', xover: 'sub_xo' } },
      { label: 'Out 8', enabled: false, hp: { mode: 'off' }, lp: { mode: 'off' } },
    ],
  };
}

describe('passband resolution', () => {
  it('resolves hp/lp crossover references, manual filters, and defaults', () => {
    const passbands = resolvePassbands(makePreset());
    expect(passbands).toHaveLength(8);

    const byLabel = new Map(passbands.map((p) => [p.label, p]));
    expect(byLabel.get('L Low')).toMatchObject({ fLo: 80, fHi: 2500, enabled: true });
    expect(byLabel.get('R Low')).toMatchObject({ fLo: 80, fHi: 2500 });
    expect(byLabel.get('L High')).toMatchObject({ fLo: 2500, fHi: 20000 });
    expect(byLabel.get('R High')).toMatchObject({ fLo: 3000, fHi: 20000 });

    // No hp, no lp at all - the wide-open default in both directions.
    expect(byLabel.get('Full Range')).toMatchObject({ fLo: 20, fHi: 20000 });

    // Sub: no hp, lp references the sub crossover.
    expect(byLabel.get('Sub 1')).toMatchObject({ fLo: 20, fHi: 80 });

    // Disabled outputs still resolve a band (planTapBudget is what zeroes
    // them), but their enabled flag carries through.
    expect(byLabel.get('Sub 2')).toMatchObject({ enabled: false, fLo: 20, fHi: 80 });
    expect(byLabel.get('Out 8')).toMatchObject({ enabled: false, fLo: 20, fHi: 20000 });
  });

  it('an hp/lp mode of "off" with a stale xover id still defaults wide-open', () => {
    const preset = {
      crossovers: [{ id: 'sub_xo', freq: 80 }],
      outputs: [
        { label: 'A', enabled: true, hp: { mode: 'off', xover: 'sub_xo' }, lp: { mode: 'off' } },
      ],
    };
    expect(resolvePassbands(preset)[0]).toMatchObject({ fLo: 20, fHi: 20000 });
  });

  it('a manual filter referencing no crossover id resolves from its own freq', () => {
    const preset = {
      crossovers: [],
      outputs: [
        { label: 'A', enabled: true, hp: { mode: 'manual', freq: 100, type: 'LR4' }, lp: { mode: 'manual', freq: 5000, type: 'LR4' } },
      ],
    };
    expect(resolvePassbands(preset)[0]).toMatchObject({ fLo: 100, fHi: 5000 });
  });

  it('feeds directly into planTapBudget: subs and disabled outputs get zero taps', () => {
    const plan = planWizardTapBudget(makePreset());
    const byLabel = new Map(plan.map((p) => [p.label, p]));
    expect(byLabel.get('Sub 1').taps).toBe(0); // below planTapBudget's default subCutoffHz (120)
    expect(byLabel.get('Sub 2').taps).toBe(0); // disabled
    expect(byLabel.get('Out 8').taps).toBe(0); // disabled
    expect(byLabel.get('L Low').taps).toBeGreaterThan(0);
    expect(byLabel.get('L Low').taps % 128).toBe(0);
  });
});

describe('naming: generation and filename assignment', () => {
  it('starts at generation 1 on a fresh file list', () => {
    expect(nextGeneration([], ['Woofer L', 'Woofer R'])).toBe(1);
  });

  it('slugifies labels: lowercase, spaces to dashes, punctuation stripped', () => {
    expect(slugifyLabel('L Woofer')).toBe('l-woofer');
    expect(slugifyLabel('Sub 1')).toBe('sub-1');
    expect(slugifyLabel('  R/High (2)  ')).toBe('r-high-2');
    expect(slugifyLabel('')).toBe('output');
  });

  it('bumps past a generation where ANY of this batch\'s filenames collide', () => {
    // gen 1 has a woofer-l file already, even though woofer-r doesn't -
    // the whole batch must move to gen 2 together (shared generation number).
    const existing = ['a1-woofer-l.bin', 'a1-tweeter-l.bin'];
    expect(nextGeneration(existing, ['Woofer L', 'Woofer R'])).toBe(2);
  });

  it('is a no-op bump when the collision is on an unrelated label', () => {
    const existing = ['a1-something-else.bin'];
    expect(nextGeneration(existing, ['Woofer L', 'Woofer R'])).toBe(1);
  });

  it('skips every generation with any collision, not just the first', () => {
    const existing = ['a1-woofer-l.bin', 'a2-woofer-l.bin', 'a3-woofer-l.bin'];
    expect(nextGeneration(existing, ['Woofer L'])).toBe(4);
  });

  it('disambiguates two outputs sharing the same label within one batch', () => {
    const filenames = assignFilenames(1, [
      { index: 0, label: 'Sub' },
      { index: 1, label: 'Sub' },
    ]);
    expect(filenames[0]).toBe('a1-sub.bin');
    expect(filenames[1]).toBe('a1-sub-2.bin');
    expect(filenames[0]).not.toBe(filenames[1]);
  });
});

describe('the apply-plan builder', () => {
  it('orders copy -> upload each -> assign each -> delays -> enable, using fresh names', () => {
    const kernelA = Float32Array.from([1, 0, 0, 0]);
    const kernelB = Float32Array.from([0, 1, 0, 0]);
    const plan = buildApplyPlan({
      sourcePresetName: 'Home',
      destPresetName: 'Home Corrected',
      existingFirFiles: ['a1-woofer-l.bin'],
      outputs: [
        { index: 0, label: 'Woofer L', kernel: kernelA, delayUs: 120 },
        { index: 1, label: 'Woofer R', kernel: kernelB, delayUs: null },
      ],
    });

    expect(plan[0]).toEqual({ method: 'copyPreset', args: ['Home', 'Home Corrected'] });
    const uploads = plan.filter((s) => s.method === 'uploadFir');
    expect(uploads).toHaveLength(2);
    // gen 1 collides on woofer-l, so the whole batch moves to gen 2
    expect(uploads[0].args[0]).toBe('a2-woofer-l.bin');
    expect(uploads[1].args[0]).toBe('a2-woofer-r.bin');
    expect(uploads[0].args[1]).toBeInstanceOf(ArrayBuffer);

    const assigns = plan.filter((s) => s.method === 'setOutputFir');
    expect(assigns).toEqual([
      { method: 'setOutputFir', args: ['Home Corrected', 0, 'a2-woofer-l.bin'] },
      { method: 'setOutputFir', args: ['Home Corrected', 1, 'a2-woofer-r.bin'] },
    ]);

    // Only the output with a measured delay gets a delay write.
    const delays = plan.filter((s) => s.method === 'setOutputDelay');
    expect(delays).toEqual([{ method: 'setOutputDelay', args: ['Home Corrected', 0, 120] }]);

    // Writing any delay also turns the copy's master delay toggle on -
    // otherwise the freshly-measured value sits inert.
    expect(plan[plan.length - 2]).toEqual({ method: 'setSpeakerDelayEnabled', args: ['Home Corrected', true] });
    expect(plan[plan.length - 1]).toEqual({ method: 'updateFIREnabled', args: ['Home Corrected', true] });
  });

  it('skips the delay-enable step when no output has a measured delay', () => {
    const plan = buildApplyPlan({
      sourcePresetName: 'Home',
      destPresetName: 'Home Corrected',
      existingFirFiles: [],
      outputs: [{ index: 0, label: 'Woofer L', kernel: Float32Array.from([1]), delayUs: null }],
    });
    expect(plan.some((s) => s.method === 'setSpeakerDelayEnabled')).toBe(false);
    expect(plan[plan.length - 1]).toEqual({ method: 'updateFIREnabled', args: ['Home Corrected', true] });
  });
});

describe('sweep probeEvent parsing', () => {
  it('parses a START line into schedule + enabled-output order', () => {
    const parsed = parseSweepStartLine('START 13 2 65536 197222 131072 20.00 20000.00 512');
    expect(parsed.type).toBe('start');
    expect(parsed.order).toEqual([0, 2, 3]); // mask 13 = 0b1101
    expect(parsed.schedule).toMatchObject({
      nPasses: 2, preRoll: 65536, spacing: 197222, chirpSamples: 131072,
      f0: 20, f1: 20000, fade: 512, deviceRate: 44100,
    });
  });

  it('parses CHIRP/WARN/DONE/STOP/ERR lines', () => {
    expect(parseSweepEventLine('CHIRP 2 3')).toEqual({ type: 'chirp', slot: 2, output: 3 });
    expect(parseSweepEventLine('WARN unrouted 4')).toEqual({ type: 'warn', reason: 'unrouted', output: 4 });
    expect(parseSweepEventLine('DONE')).toEqual({ type: 'done' });
    expect(parseSweepEventLine('STOP')).toEqual({ type: 'stop' });
    expect(parseSweepEventLine('ERR badParam')).toEqual({ type: 'err', reason: 'badParam' });
    expect(parseSweepEventLine('garbage')).toBeNull();
  });
});

describe('reachHz', () => {
  it('is null for a zero (minimum-phase) budget and rate/samples otherwise', () => {
    expect(reachHz(0, RATE)).toBeNull();
    expect(reachHz(10, RATE)).toBeCloseTo(RATE / Math.round(0.01 * RATE), 0);
  });
});

describe('computeRelativeDelays', () => {
  it('normalizes to the latest arrival and clamps to the device range', () => {
    const arrivals = [
      { output: 0, delayUs: 1000 },
      { output: 1, delayUs: 4000 }, // latest -> becomes the zero reference
    ];
    const result = computeRelativeDelays(arrivals, {}, 20000);
    const byOutput = new Map(result.map((r) => [r.output, r]));
    expect(byOutput.get(1).newDelayUs).toBe(0);
    expect(byOutput.get(0).newDelayUs).toBe(3000);
    expect(result.every((r) => !r.clamped)).toBe(true);
  });

  it('clamps an offset beyond the device max and flags it', () => {
    const arrivals = [
      { output: 0, delayUs: 0 },
      { output: 1, delayUs: 25000 },
    ];
    const result = computeRelativeDelays(arrivals, {}, 20000);
    const byOutput = new Map(result.map((r) => [r.output, r]));
    expect(byOutput.get(0).newDelayUs).toBe(20000);
    expect(byOutput.get(0).clamped).toBe(true);
  });
});

describe('stage 4: the house-curve target', () => {
  const PLANT = { band: { lo: 100, hi: 16000 }, resonances: [{ freq: 800, q: 2, gainDb: 5 }] };
  const measurement = measurementOf(makeSystemIr(RATE, PLANT), RATE, {
    fLo: 200, fHi: 10000, pointsPerOctave: 24,
  });
  const OUTPUT = { fLo: 300, fHi: 8000, taps: 2048 };

  function median(values) {
    const arr = [...values].sort((a, b) => a - b);
    const mid = Math.floor(arr.length / 2);
    return arr.length % 2 ? arr[mid] : (arr[mid - 1] + arr[mid]) / 2;
  }

  it('re-centers the curve over the output passband, not the analyzer window', () => {
    const tweeter = { fLo: 2500, fHi: 20000 };
    const db = targetDbForOutput(measurement, tweeter, { mode: 'harman' });
    // The passband is clipped to what was measured (200-10000 Hz here), so
    // the median that reads 0 is the one over 2500-10000.
    const inBand = [...db].filter(
      (v, i) => measurement.freqs[i] >= 2500 && measurement.freqs[i] <= 10000
    );
    expect(median(inBand)).toBeCloseTo(0, 10);
    expect(targetDbForOutput(measurement, tweeter, null)).toBeNull();
  });

  it('designs to the target: a -1 dB/oct tilt shows up as exactly that slope', () => {
    const flat = designOutputKernel(measurement, OUTPUT, { sampleRate: RATE, latencyBudgetMs: 0 });
    const tilted = designOutputKernel(measurement, OUTPUT, {
      sampleRate: RATE,
      latencyBudgetMs: 0,
      target: { mode: 'tilt', tiltDbPerOct: -1 },
    });
    const freqs = Float64Array.from([500, 1000, 2000, 4000]);
    const flatDb = freqResponse(flat, RATE, freqs).magDb;
    const tiltedDb = freqResponse(tilted, RATE, freqs).magDb;
    for (let i = 1; i < freqs.length; i++) {
      const extraSlope = (tiltedDb[i] - tiltedDb[i - 1]) - (flatDb[i] - flatDb[i - 1]);
      expect(extraSlope).toBeCloseTo(-1, 1);
    }
  });

  it('an explicit flat target is the same kernel as no target at all', () => {
    const none = designOutputKernel(measurement, OUTPUT, { sampleRate: RATE, latencyBudgetMs: 0 });
    const flat = designOutputKernel(measurement, OUTPUT, {
      sampleRate: RATE, latencyBudgetMs: 0, target: { mode: 'flat' },
    });
    for (let i = 0; i < none.length; i++) expect(flat[i]).toBe(none[i]);
  });

  it('corrects toward the target across the whole band, not just its slope', () => {
    const target = { mode: 'bk' };
    const kernel = designOutputKernel(measurement, OUTPUT, {
      sampleRate: RATE, latencyBudgetMs: 0, target,
    });
    const freqs = logSpace(OUTPUT.fLo, OUTPUT.fHi, 12);
    const system = makeSystemIr(RATE, PLANT);
    const corrected = freqResponse(convolve(kernel, system), RATE, freqs).magDb;
    const want = targetDbForOutput({ freqs }, OUTPUT, target);
    const residual = [...corrected].map((db, i) => db - want[i]);
    // Same tolerance the flat criterion uses (acceptance criterion 3).
    expect(Math.max(...residual) - Math.min(...residual)).toBeLessThanOrEqual(2);
  });
});

describe('end-to-end: synthetic two-output sweep session', () => {
  // Test-sized schedule (small powers of 2 keep the FFTs fast without
  // changing any of the math under test) - mirrors the shape of
  // tests/unit/fir-design-acceptance.test.js's own SCHEDULE, but this file
  // does not import it (that test file is off-limits; this is separately
  // authored data, not shared code).
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
  const ORDER = [0, 1];
  const KNOWN_DELAY_DIFF_US = 1200; // output 1 arrives 1.2ms after output 0
  const DRIFT_PPM = 50;
  const PRE_ROLL_EXTRA = 22050; // half a second of leading silence before the
                                // session "starts" - exercises the anchor
                                // search rather than trivially finding it at t=0

  // `distortA` (a2 coefficient) makes output 0's chain clip softly before
  // the room gets it - x + a2*x^2, so the 2nd harmonic comes out at a2/2 of
  // the fundamental. Default 0 = the linear session the other tests use.
  function buildSession(distortA = 0) {
    const clean = generateSweep(RATE, SCHEDULE);
    const ref = clean;
    const drive = distortA === 0 ? clean : Float32Array.from(clean, (x) => x + distortA * x * x);
    const systemA = makeSystemIr(RATE, {
      delayS: 0.004,
      band: { lo: 100, hi: 16000 },
      resonances: [{ freq: 800, q: 2, gainDb: 5 }],
    });
    const systemB = makeSystemIr(RATE, {
      delayS: 0.004 + KNOWN_DELAY_DIFF_US * 1e-6,
      band: { lo: 100, hi: 16000 },
      resonances: [{ freq: 2000, q: 2, gainDb: -4 }],
    });
    const wetA = convolve(drive, systemA);
    const wetB = convolve(ref, systemB);
    const nSlots = ORDER.length * SCHEDULE.nPasses;
    const tail = Math.max(wetA.length, wetB.length);
    const sessionLen = PRE_ROLL_EXTRA + SCHEDULE.preRoll + nSlots * SCHEDULE.spacing + tail;

    let session = new Float32Array(sessionLen);
    for (let pass = 0; pass < SCHEDULE.nPasses; pass++) {
      for (let pos = 0; pos < ORDER.length; pos++) {
        const slot = pass * ORDER.length + pos;
        const wet = ORDER[pos] === 0 ? wetA : wetB;
        const at = PRE_ROLL_EXTRA + SCHEDULE.preRoll + slot * SCHEDULE.spacing;
        for (let i = 0; i < wet.length; i++) session[at + i] += wet[i];
      }
    }
    session = stretch(session, 1 + DRIFT_PPM * 1e-6);
    const rng = makeRng(7);
    const amp = Math.pow(10, -80 / 20) * Math.sqrt(3);
    for (let i = 0; i < session.length; i++) session[i] += amp * (2 * rng() - 1);

    return { session, systemA, systemB };
  }

  it('reports harmonic distortion per output, and pins it to the output that has it', () => {
    const A2 = 0.03; // 2nd harmonic at a2/2 = 1.5% = -36.5dB of the fundamental
    const { session } = buildSession(A2);

    const result = processSweepCapture(session, RATE, SCHEDULE, ORDER, {
      bandForOutput: () => ({ fLo: 300, fHi: 8000 }),
    });
    const byOutput = new Map(result.outputs.map((o) => [o.output, o]));
    const dirty = byOutput.get(0).harmonics;
    const clean = byOutput.get(1).harmonics;

    // Median 2nd-order reading over a span where both the fundamental and
    // its harmonic sit inside the harness system's flat band.
    const medianH2 = (h) => {
      const row = h.orders.find((o) => o.order === 2);
      const vals = [];
      for (let i = 0; i < h.freqs.length; i++) {
        if (h.freqs[i] >= 3000 && h.freqs[i] <= 5000 && Number.isFinite(row.relDb[i])) {
          vals.push(row.relDb[i]);
        }
      }
      vals.sort((a, b) => a - b);
      return vals[Math.floor(vals.length / 2)];
    };

    // Not exact, and shouldn't be: what a mic hears at 2f has passed through
    // the system's own response at 2f, while the fundamental passed through
    // it at f. The harness's band rolls off toward 16kHz, which costs the
    // 6-10kHz harmonics about 0.7dB here. That is the real quantity - a
    // speaker's distortion is what it radiates, not what its motor generated.
    expect(Math.abs(medianH2(dirty) - 20 * Math.log10(A2 / 2))).toBeLessThan(1.5);
    // The other output shared the room, the capture and the noise floor but
    // not the nonlinearity - the reading has to follow the chain, not the
    // session.
    expect(medianH2(dirty) - medianH2(clean)).toBeGreaterThan(20);
  });

  it('recovers per-output magnitude shape and the inter-output delay', () => {
    const { session, systemA, systemB } = buildSession();

    const result = processSweepCapture(session, RATE, SCHEDULE, ORDER, {
      bandForOutput: () => ({ fLo: 300, fHi: 8000 }),
    });

    const byOutput = new Map(result.outputs.map((o) => [o.output, o]));
    const outA = byOutput.get(0);
    const outB = byOutput.get(1);
    expect(outA).toBeDefined();
    expect(outB).toBeDefined();

    // Magnitude shape (mean-removed, since absolute level is an arbitrary
    // deconvolution/mic-gain artifact) within 1.5 dB of the harness's own
    // independent freqResponse over the same measured frequencies.
    function worstShapeErrorDb(measured, systemIr) {
      const truth = freqResponse(systemIr, RATE, measured.freqs).magDb;
      let mMean = 0;
      let tMean = 0;
      for (let i = 0; i < measured.freqs.length; i++) {
        mMean += measured.magDb[i];
        tMean += truth[i];
      }
      mMean /= measured.freqs.length;
      tMean /= measured.freqs.length;
      let worst = 0;
      for (let i = 0; i < measured.freqs.length; i++) {
        worst = Math.max(worst, Math.abs((measured.magDb[i] - mMean) - (truth[i] - tMean)));
      }
      return worst;
    }

    expect(worstShapeErrorDb(outA.measurement, systemA)).toBeLessThanOrEqual(1.5);
    expect(worstShapeErrorDb(outB.measurement, systemB)).toBeLessThanOrEqual(1.5);

    // Inter-output delay: output B was built 1.2ms later than output A.
    const measuredDiffUs = outB.delayUs - outA.delayUs;
    expect(Math.abs(measuredDiffUs - KNOWN_DELAY_DIFF_US)).toBeLessThanOrEqual(250);

    // Sanity: drift was injected and (roughly) recovered.
    expect(Math.abs(result.driftPpm - DRIFT_PPM)).toBeLessThanOrEqual(10);
  });
});
