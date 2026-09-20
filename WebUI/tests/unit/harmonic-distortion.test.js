import { describe, it, expect } from 'vitest';
import {
  generateSweep,
  deconvolve,
  harmonicResponse,
  harmonicOffsetSeconds,
  relDbToPercent,
} from '../../src/sweep-math.js';

const RATE = 44100;
const SCHEDULE = { f0: 20, f1: 20000, chirpSamples: 65536, fade: 512, deviceRate: RATE };
const SWEEP_SECONDS = SCHEDULE.chirpSamples / SCHEDULE.deviceRate;

/**
 * Play `sweep` through a memoryless polynomial nonlinearity, delayed.
 * For x = A*sin(t):  a2*x^2 contributes a2*A^2/2 at the 2nd harmonic and
 * a3*x^3 contributes a3*A^3/4 at the 3rd, so with the sweep's A = 1 the
 * expected harmonic-to-fundamental ratios are a2/2 and a3/4.
 */
function playDistorted(sweep, { a2 = 0, a3 = 0, delay = 1000 } = {}) {
  const out = new Float32Array(sweep.length + delay);
  for (let i = 0; i < sweep.length; i++) {
    const x = sweep[i];
    out[i + delay] = x + a2 * x * x + a3 * x * x * x;
  }
  return out;
}

function measure(segment, opts = {}) {
  const ref = generateSweep(RATE, SCHEDULE);
  const { ir, preIr } = deconvolve(segment, ref, RATE);
  return harmonicResponse(ir, preIr, RATE, {
    fLo: 100, fHi: 5000, f0: SCHEDULE.f0, f1: SCHEDULE.f1,
    sweepSeconds: SWEEP_SECONDS, ...opts,
  });
}

/** Median relDb of one order over a frequency span, ignoring NaN. */
function medianOver(result, order, loHz, hiHz) {
  const row = result.orders.find((o) => o.order === order);
  const vals = [];
  for (let i = 0; i < result.freqs.length; i++) {
    const f = result.freqs[i];
    if (f >= loHz && f <= hiHz && Number.isFinite(row.relDb[i])) vals.push(row.relDb[i]);
  }
  vals.sort((a, b) => a - b);
  return vals.length ? vals[Math.floor(vals.length / 2)] : NaN;
}

describe('harmonicOffsetSeconds', () => {
  it('matches Farina for the wizard chirp (2.97s, 20Hz-20kHz)', () => {
    const T = 131072 / 44100;
    const ms = (k) => harmonicOffsetSeconds(k, T, 20, 20000) * 1000;
    expect(ms(2)).toBeCloseTo(298, 0);
    expect(ms(3)).toBeCloseTo(473, 0);
    expect(ms(4)).toBeCloseTo(596, 0);
    expect(ms(5)).toBeCloseTo(692, 0);
  });

  it('is zero for the linear "first order"', () => {
    expect(harmonicOffsetSeconds(1, 3, 20, 20000)).toBe(0);
  });
});

describe('harmonicResponse', () => {
  it('recovers a known 2nd-order nonlinearity', () => {
    const sweep = generateSweep(RATE, SCHEDULE);
    const a2 = 0.02; // -> 2nd harmonic 1% of the fundamental = -40dB
    const result = measure(playDistorted(sweep, { a2 }));
    const expected = 20 * Math.log10(a2 / 2); // x^2 adds DC and 2f, nothing at f
    expect(medianOver(result, 2, 200, 3000)).toBeCloseTo(expected, 1);
  });

  it('recovers a known 3rd-order nonlinearity', () => {
    const sweep = generateSweep(RATE, SCHEDULE);
    const a3 = 0.08;
    const result = measure(playDistorted(sweep, { a3 }));
    // The cubic term also lifts the fundamental (A + 3*a3*A^3/4), and the
    // ratio is referenced to the fundamental that actually came out - the
    // measurement tracks that to ~0.003dB, so the tolerance here is slack.
    const expected = 20 * Math.log10((a3 / 4) / (1 + (3 * a3) / 4));
    expect(medianOver(result, 3, 200, 3000)).toBeCloseTo(expected, 1);
  });

  it('separates the orders: a pure 2nd-order system shows little 3rd', () => {
    const sweep = generateSweep(RATE, SCHEDULE);
    const result = measure(playDistorted(sweep, { a2: 0.02 }));
    const h2 = medianOver(result, 2, 200, 3000);
    const h3 = medianOver(result, 3, 200, 3000);
    expect(h2 - h3).toBeGreaterThan(20);
  });

  it('reports a floor well below a real reading, and finds no distortion in a linear system', () => {
    const sweep = generateSweep(RATE, SCHEDULE);
    const clean = measure(playDistorted(sweep, {}));
    const floors = [...clean.floorDb].filter(Number.isFinite);
    expect(floors.length).toBeGreaterThan(0);
    // A linear system's "harmonics" are deconvolution residue, not signal:
    // they must sit near the floor rather than standing above it.
    const h2 = medianOver(clean, 2, 200, 3000);
    expect(h2).toBeLessThan(-60);

    const dirty = measure(playDistorted(sweep, { a2: 0.02 }));
    const dirtyFloor = [...dirty.floorDb].filter(Number.isFinite);
    const medFloor = dirtyFloor.sort((a, b) => a - b)[Math.floor(dirtyFloor.length / 2)];
    expect(medianOver(dirty, 2, 200, 3000) - medFloor).toBeGreaterThan(15);
  });

  it('totals the orders into thdDb and converts to percent', () => {
    const sweep = generateSweep(RATE, SCHEDULE);
    const result = measure(playDistorted(sweep, { a2: 0.02, a3: 0.08 }));
    // 1% (2nd) and 2% (3rd) in quadrature = sqrt(5)% = 2.24%
    const i = result.freqs.findIndex((f) => f >= 1000);
    expect(relDbToPercent(result.thdDb[i])).toBeGreaterThan(1.6);
    expect(relDbToPercent(result.thdDb[i])).toBeLessThan(3.0);
  });

  it('leaves NaN where the harmonic would land above Nyquist', () => {
    const sweep = generateSweep(RATE, SCHEDULE);
    const result = measure(playDistorted(sweep, { a2: 0.02 }), { fHi: 20000 });
    const row = result.orders.find((o) => o.order === 5);
    const top = result.freqs.findIndex((f) => f * 5 > RATE / 2);
    expect(top).toBeGreaterThan(0);
    expect(Number.isFinite(row.relDb[top])).toBe(false);
  });
});
