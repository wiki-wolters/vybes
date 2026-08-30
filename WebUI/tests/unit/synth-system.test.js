// Self-tests for the auto-FIR verification harness (tests/helpers/
// synth-system.js). These must stay green on their own: the acceptance
// suite (fir-design-acceptance.test.js) trusts this machinery, so this file
// is what makes that trust earned rather than circular.
import { describe, it, expect } from 'vitest';
import {
  fft,
  nextPow2,
  makeRng,
  makeSystemIr,
  convolve,
  freqResponse,
  logSpace,
  stretch,
  simulateCapture,
  excessPhaseOf,
  allpassPhase,
  rms,
  argmaxAbs,
} from '../helpers/synth-system.js';

const RATE = 44100;

describe('fft', () => {
  it('round-trips a random signal', () => {
    const rng = makeRng(42);
    const n = 1024;
    const re = new Float64Array(n);
    const im = new Float64Array(n);
    const orig = new Float64Array(n);
    for (let i = 0; i < n; i++) {
      orig[i] = 2 * rng() - 1;
      re[i] = orig[i];
    }
    fft(re, im, false);
    fft(re, im, true);
    for (let i = 0; i < n; i++) {
      expect(re[i]).toBeCloseTo(orig[i], 9);
      expect(im[i]).toBeCloseTo(0, 9);
    }
  });

  it('gives a flat spectrum for a delta', () => {
    const n = 256;
    const re = new Float64Array(n);
    const im = new Float64Array(n);
    re[0] = 1;
    fft(re, im, false);
    for (let k = 0; k < n; k++) {
      expect(Math.hypot(re[k], im[k])).toBeCloseTo(1, 9);
    }
  });
});

describe('convolve', () => {
  it('matches direct convolution on short arrays', () => {
    const a = Float32Array.from([1, 2, 3]);
    const b = Float32Array.from([4, 5]);
    const out = convolve(a, b);
    const expected = [4, 13, 22, 15]; // (1,2,3)*(4,5)
    expect(out.length).toBe(4);
    for (let i = 0; i < 4; i++) expect(out[i]).toBeCloseTo(expected[i], 5);
  });

  it('a shifted delta shifts the signal', () => {
    const sig = Float32Array.from([1, -2, 0.5, 0.25]);
    const delta = new Float32Array(8);
    delta[3] = 1;
    const out = convolve(sig, delta);
    for (let i = 0; i < sig.length; i++) expect(out[3 + i]).toBeCloseTo(sig[i], 6);
  });
});

describe('makeSystemIr', () => {
  it('puts a pure delay where it belongs, causally and compactly', () => {
    const delayS = 0.005;
    const ir = makeSystemIr(RATE, { delayS, band: { lo: 20, hi: 20000 } });
    const peak = argmaxAbs(ir);
    expect(Math.abs(peak - delayS * RATE)).toBeLessThanOrEqual(3);
    let around = 0;
    let total = 0;
    for (let i = 0; i < ir.length; i++) {
      total += ir[i] * ir[i];
      if (Math.abs(i - peak) <= 64) around += ir[i] * ir[i];
    }
    expect(around / total).toBeGreaterThan(0.8);
    // Minimum-phase construction is causal: nothing before the delay.
    let preEnergy = 0;
    for (let i = 0; i < peak - 32; i++) preEnergy += ir[i] * ir[i];
    expect(preEnergy / total).toBeLessThan(0.01);
  });

  it('renders a resonance at its specified gain', () => {
    const flat = makeSystemIr(RATE, { band: { lo: 100, hi: 16000 } });
    const peaked = makeSystemIr(RATE, {
      band: { lo: 100, hi: 16000 },
      resonances: [{ freq: 1000, q: 2, gainDb: 6 }],
    });
    const freqs = Float64Array.from([1000]);
    const a = freqResponse(flat, RATE, freqs).magDb[0];
    const b = freqResponse(peaked, RATE, freqs).magDb[0];
    expect(b - a).toBeCloseTo(6, 0.5);
  });

  it('renders a reflection as the right comb depth', () => {
    // g = 0.5 at 1 ms: constructive at 1 kHz (|1+g| = 1.5), destructive at
    // 500 Hz (|1-g| = 0.5) -> 20*log10(3) = 9.54 dB peak-to-trough.
    const ir = makeSystemIr(RATE, {
      band: { lo: 100, hi: 16000 },
      reflection: { delayS: 0.001, gain: 0.5 },
    });
    const { magDb } = freqResponse(ir, RATE, Float64Array.from([1000, 500]));
    expect(magDb[0] - magDb[1]).toBeCloseTo(9.54, 0);
  });

  it('inverts with polarity', () => {
    const pos = makeSystemIr(RATE, { delayS: 0.002 });
    const neg = makeSystemIr(RATE, { delayS: 0.002, polarity: -1 });
    const peak = argmaxAbs(pos);
    expect(neg[peak]).toBeCloseTo(-pos[peak], 6);
  });
});

describe('stretch', () => {
  it('scales the length by the factor', () => {
    const sig = new Float32Array(10000);
    expect(stretch(sig, 1.0001).length).toBe(10001);
    expect(stretch(sig, 0.9999).length).toBe(9999);
  });

  it('reproduces a linear ramp exactly in the interior', () => {
    const n = 1000;
    const sig = new Float32Array(n);
    for (let i = 0; i < n; i++) sig[i] = i * 0.01;
    const out = stretch(sig, 1.5);
    for (let i = 10; i < out.length - 10; i++) {
      expect(out[i]).toBeCloseTo((i / 1.5) * 0.01, 4);
    }
  });
});

describe('simulateCapture', () => {
  it('is deterministic per seed', () => {
    const ir = makeSystemIr(RATE, { delayS: 0.001 });
    const stim = new Float32Array(512).fill(0);
    stim[0] = 1;
    const a = simulateCapture(ir, stim, { seed: 7 });
    const b = simulateCapture(ir, stim, { seed: 7 });
    expect(a).toEqual(b);
    const c = simulateCapture(ir, stim, { seed: 8 });
    expect(c).not.toEqual(a);
  });

  it('places the system response after the pre-roll', () => {
    const delayS = 0.002;
    const ir = makeSystemIr(RATE, { delayS });
    const stim = new Float32Array(256);
    stim[0] = 1;
    const cap = simulateCapture(ir, stim, {
      sampleRate: RATE,
      preRollS: 0.05,
      noiseDbFs: -100,
    });
    const peak = argmaxAbs(cap);
    const expected = Math.round((0.05 + delayS) * RATE);
    expect(Math.abs(peak - expected)).toBeLessThanOrEqual(3);
  });
});

describe('excessPhaseOf', () => {
  const freqs = logSpace(300, 8000, 24);

  it('reads near zero on a minimum-phase system', () => {
    const ir = makeSystemIr(RATE, {
      band: { lo: 100, hi: 16000 },
      resonances: [{ freq: 700, q: 2, gainDb: 4 }],
    });
    expect(rms(excessPhaseOf(ir, RATE, freqs))).toBeLessThan(0.05);
  });

  it('sees an allpass as substantial excess phase', () => {
    const ir = makeSystemIr(RATE, {
      band: { lo: 100, hi: 16000 },
      allpasses: [{ freq: 1000, q: 1 }],
    });
    expect(rms(excessPhaseOf(ir, RATE, freqs))).toBeGreaterThan(0.3);
  });

  it('ignores bulk delay', () => {
    const near = makeSystemIr(RATE, { band: { lo: 100, hi: 16000 } });
    const far = makeSystemIr(RATE, { band: { lo: 100, hi: 16000 }, delayS: 0.004 });
    const a = rms(excessPhaseOf(near, RATE, freqs));
    const b = rms(excessPhaseOf(far, RATE, freqs));
    expect(Math.abs(a - b)).toBeLessThan(0.05);
  });
});

describe('allpassPhase', () => {
  it('rotates from 0 toward -2*pi through -pi at center', () => {
    const ap = { freq: 1000, q: 1 };
    expect(allpassPhase(10, ap)).toBeCloseTo(0, 1);
    expect(allpassPhase(1000, ap)).toBeCloseTo(-Math.PI, 3);
    expect(allpassPhase(40000, ap)).toBeLessThan(-2 * Math.PI + 0.3);
  });
});

describe('utilities', () => {
  it('nextPow2', () => {
    expect(nextPow2(1)).toBe(1);
    expect(nextPow2(1025)).toBe(2048);
  });

  it('logSpace spans the band at the requested density', () => {
    const g = logSpace(1000, 4000, 12);
    expect(g[0]).toBeCloseTo(1000, 6);
    expect(g.length).toBe(25); // two octaves at 12/oct, inclusive
    expect(g[24] / g[23]).toBeCloseTo(Math.pow(2, 1 / 12), 6);
  });
});
