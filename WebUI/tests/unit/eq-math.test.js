import { describe, it, expect } from 'vitest'
import { peakingBellDb, peqSumDb, octavesToQ, fitPeqPoints } from '../../src/eq-math.js'

/*
 * Independent reference implementation of the RBJ analog-prototype peaking
 * bell magnitude, evaluated with explicit complex arithmetic (double
 * precision) rather than the closed-form magnitude expression used by the
 * production code:
 *
 *   H(s) = (s^2 + s*(A/Q) + 1) / (s^2 + s/(A*Q) + 1),  s = j*(f/f0)
 *   A = 10^(gainDb/40)
 */
function referencePeakingDb(freq, centerFreq, gainDb, q) {
  const A = Math.pow(10, gainDb / 40)
  const w = freq / centerFreq
  // s = j*w  =>  s^2 = -w^2
  const numRe = 1 - w * w
  const numIm = (A / q) * w
  const denRe = 1 - w * w
  const denIm = w / (A * q)
  const magSq = (numRe * numRe + numIm * numIm) / (denRe * denRe + denIm * denIm)
  return 10 * Math.log10(magSq)
}

const CENTERS = [50, 250, 1000, 4000, 12000]
const GAINS = [-15, -9, -3, -0.5, 0.5, 3, 9, 15]
const QS = [0.1, 0.5, 1, 2.5, 10]

describe('peakingBellDb', () => {
  it('matches the independent RBJ reference across a freq/gain/Q grid', () => {
    for (const fc of CENTERS) {
      for (const gain of GAINS) {
        for (const q of QS) {
          // At center, off center (near and far, both sides), and extremes
          const freqs = [
            fc,
            fc * Math.pow(2, 0.1), fc / Math.pow(2, 0.1),
            fc * 2, fc / 2,
            fc * 8, fc / 8,
            20, 20000,
          ]
          for (const f of freqs) {
            const actual = peakingBellDb(f, fc, gain, q)
            const expected = referencePeakingDb(f, fc, gain, q)
            expect(actual, `f=${f} fc=${fc} gain=${gain} q=${q}`).toBeCloseTo(expected, 10)
          }
        }
      }
    }
  })

  it('reaches exactly the specified gain at the center frequency', () => {
    for (const gain of GAINS) {
      for (const q of QS) {
        expect(peakingBellDb(1000, 1000, gain, q)).toBeCloseTo(gain, 10)
      }
    }
  })

  it('is symmetric on the log-frequency axis', () => {
    // |H(j*w)| == |H(j/w)| for the analog prototype
    for (const ratio of [1.5, 2, 4, 10]) {
      const above = peakingBellDb(1000 * ratio, 1000, 6, 1.4)
      const below = peakingBellDb(1000 / ratio, 1000, 6, 1.4)
      expect(above).toBeCloseTo(below, 10)
    }
  })

  it('negative gain mirrors positive gain exactly (cut is inverse of boost)', () => {
    for (const f of [100, 500, 1000, 3000, 15000]) {
      const boost = peakingBellDb(f, 1000, 9, 2)
      const cut = peakingBellDb(f, 1000, -9, 2)
      expect(cut).toBeCloseTo(-boost, 10)
    }
  })

  it('decays toward 0 dB far from the center', () => {
    expect(Math.abs(peakingBellDb(20, 1000, 12, 4))).toBeLessThan(0.1)
    expect(Math.abs(peakingBellDb(20000, 1000, 12, 4))).toBeLessThan(0.1)
  })

  it('returns 0 for degenerate inputs', () => {
    expect(peakingBellDb(1000, 1000, 0, 1)).toBe(0) // no gain
    expect(peakingBellDb(1000, 1000, 6, 0)).toBe(0) // non-positive Q
    expect(peakingBellDb(1000, 1000, 6, -1)).toBe(0)
    expect(peakingBellDb(1000, 0, 6, 1)).toBe(0) // non-positive center
    expect(peakingBellDb(0, 1000, 6, 1)).toBe(0) // non-positive freq
  })
})

describe('peqSumDb', () => {
  it('is the sum of the individual bell responses', () => {
    const points = [
      { freq: 100, gain: 6, q: 1 },
      { freq: 1000, gain: -4, q: 2 },
      { freq: 8000, gain: 2.5, q: 0.7 },
    ]
    for (const f of [50, 100, 315, 1000, 4000, 8000, 16000]) {
      const expected = points.reduce((acc, p) => acc + peakingBellDb(f, p.freq, p.gain, p.q), 0)
      expect(peqSumDb(points, f)).toBeCloseTo(expected, 12)
    }
  })

  it('is 0 for an empty set', () => {
    expect(peqSumDb([], 1000)).toBe(0)
  })
})

describe('octavesToQ', () => {
  it('matches the standard 2^(N/2) / (2^N - 1) formula', () => {
    for (const n of [1 / 3, 0.5, 1, 1.5, 2, 3, 5]) {
      expect(octavesToQ(n)).toBeCloseTo(Math.pow(2, n / 2) / (Math.pow(2, n) - 1), 12)
    }
  })

  it('gives the well-known Q ~= 1.414 for a one-octave bandwidth', () => {
    expect(octavesToQ(1)).toBeCloseTo(Math.SQRT2, 12)
  })

  it('narrower bandwidth means higher Q', () => {
    expect(octavesToQ(1 / 3)).toBeGreaterThan(octavesToQ(1))
    expect(octavesToQ(1)).toBeGreaterThan(octavesToQ(3))
  })
})

describe('fitPeqPoints', () => {
  // 1/3-octave band centers from 20 Hz to 20 kHz, like the analyzer uses
  const freqs = Array.from({ length: 31 }, (_, i) => 20 * Math.pow(2, i / 3))

  it('returns no points for a flat correction', () => {
    expect(fitPeqPoints(freqs, freqs.map(() => 0))).toEqual([])
  })

  it('returns no points when every band is NaN', () => {
    expect(fitPeqPoints(freqs, freqs.map(() => NaN))).toEqual([])
  })

  it('ignores corrections below minGainDb', () => {
    expect(fitPeqPoints(freqs, freqs.map(() => 0.5))).toEqual([])
  })

  it('recovers a single synthetic bell', () => {
    const target = { freq: 1000, gain: 6, q: 1 }
    const correction = freqs.map((f) => peakingBellDb(f, target.freq, target.gain, target.q))

    const points = fitPeqPoints(freqs, correction)
    expect(points.length).toBeGreaterThanOrEqual(1)

    // The dominant point sits near 1 kHz with positive gain...
    const main = points.reduce((a, b) => (Math.abs(b.gain) > Math.abs(a.gain) ? b : a))
    expect(main.freq).toBeGreaterThan(500)
    expect(main.freq).toBeLessThan(2000)
    expect(main.gain).toBeGreaterThan(0)

    // ...and the fitted ensemble tracks the correction closely
    for (let i = 0; i < freqs.length; i++) {
      const fitted = peqSumDb(points, freqs[i])
      expect(Math.abs(fitted - correction[i]), `at ${freqs[i].toFixed(0)} Hz`).toBeLessThan(1.5)
    }
  })

  it('fits cuts (negative corrections) too', () => {
    const correction = freqs.map((f) => peakingBellDb(f, 250, -8, 2))
    const points = fitPeqPoints(freqs, correction)
    expect(points.length).toBeGreaterThanOrEqual(1)
    const main = points.reduce((a, b) => (Math.abs(b.gain) > Math.abs(a.gain) ? b : a))
    expect(main.gain).toBeLessThan(0)
  })

  it('respects maxBands', () => {
    // A wiggly multi-bump correction that would want many filters
    const correction = freqs.map((f, i) => 8 * Math.sin(i * 1.1))
    const points = fitPeqPoints(freqs, correction, { maxBands: 3 })
    expect(points.length).toBeLessThanOrEqual(3)
  })

  it('clamps gains to gainLimit and Q into [qMin, qMax]', () => {
    const correction = freqs.map((f) => (f > 900 && f < 1100 ? 30 : 0))
    const points = fitPeqPoints(freqs, correction)
    for (const p of points) {
      expect(Math.abs(p.gain)).toBeLessThanOrEqual(15)
      expect(p.q).toBeGreaterThanOrEqual(0.4)
      expect(p.q).toBeLessThanOrEqual(10)
    }
  })

  it('skips NaN bands but fits the valid ones', () => {
    const correction = freqs.map((f) =>
      f > 800 && f < 1300 ? 6 : NaN
    )
    const points = fitPeqPoints(freqs, correction)
    expect(points.length).toBeGreaterThanOrEqual(1)
    for (const p of points) {
      expect(Number.isFinite(p.freq)).toBe(true)
      expect(Number.isFinite(p.gain)).toBe(true)
      expect(Number.isFinite(p.q)).toBe(true)
    }
  })

  it('returns points sorted by frequency with rounded values', () => {
    const correction = freqs.map(
      (f) => peakingBellDb(f, 100, 8, 1.5) + peakingBellDb(f, 5000, -7, 1.5)
    )
    const points = fitPeqPoints(freqs, correction)
    expect(points.length).toBeGreaterThanOrEqual(2)

    for (let i = 1; i < points.length; i++) {
      expect(points[i].freq).toBeGreaterThan(points[i - 1].freq)
    }
    for (const p of points) {
      // freq and gain rounded to 0.1, q to 0.01
      expect(p.freq).toBeCloseTo(Math.round(p.freq * 10) / 10, 12)
      expect(p.gain).toBeCloseTo(Math.round(p.gain * 10) / 10, 12)
      expect(p.q).toBeCloseTo(Math.round(p.q * 100) / 100, 12)
    }
  })
})
