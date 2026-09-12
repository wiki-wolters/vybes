import { describe, it, expect } from 'vitest'
import { peakingBellDb, peqSumDb, octavesToQ, fitPeqPoints, peqPointsMatch } from '../../src/eq-math.js'
import { DEVICE_SAMPLE_RATE } from '../../src/device.js'

/*
 * Independent reference: the RBJ audio-EQ-cookbook peaking biquad, cooked
 * from the cookbook's own alpha / cos(w0) recipe and evaluated on the unit
 * circle with explicit complex arithmetic. The production code takes a
 * different route (the analog prototype's closed-form magnitude at the
 * bilinear-warped frequency), so agreement here is a real check that the
 * curve the UI draws is the biquad the Teensy runs.
 */
function referencePeakingDb(freq, centerFreq, gainDb, q, fs = DEVICE_SAMPLE_RATE) {
  const A = Math.pow(10, gainDb / 40)
  const w0 = 2 * Math.PI * centerFreq / fs
  const alpha = Math.sin(w0) / (2 * q)
  const b = [1 + alpha * A, -2 * Math.cos(w0), 1 - alpha * A]
  const a = [1 + alpha / A, -2 * Math.cos(w0), 1 - alpha / A]
  const w = 2 * Math.PI * freq / fs
  // sum c[k] * e^(-j*k*w)
  const poly = (c) => {
    let re = 0
    let im = 0
    for (let k = 0; k < 3; k++) {
      re += c[k] * Math.cos(k * w)
      im -= c[k] * Math.sin(k * w)
    }
    return [re, im]
  }
  const [nr, ni] = poly(b)
  const [dr, di] = poly(a)
  return 10 * Math.log10((nr * nr + ni * ni) / (dr * dr + di * di))
}

// The analog prototype the digital bell is derived from - what the UI used
// to draw. Kept only to pin down where the two agree and where they part.
function analogPeakingDb(freq, centerFreq, gainDb, q) {
  const A = Math.pow(10, gainDb / 40)
  const w = freq / centerFreq
  const numRe = 1 - w * w
  const numIm = (A / q) * w
  const denRe = 1 - w * w
  const denIm = w / (A * q)
  return 10 * Math.log10((numRe * numRe + numIm * numIm) / (denRe * denRe + denIm * denIm))
}

const NYQUIST = DEVICE_SAMPLE_RATE / 2
const CENTERS = [50, 250, 1000, 4000, 12000, 20000]
const GAINS = [-15, -9, -3, -0.5, 0.5, 3, 9, 15]
const QS = [0.1, 0.5, 1, 2.5, 10]

describe('peakingBellDb', () => {
  it('matches the independent RBJ biquad across a freq/gain/Q grid', () => {
    for (const fc of CENTERS) {
      for (const gain of GAINS) {
        for (const q of QS) {
          // At center, off center (near and far, both sides), and extremes -
          // everything the device can actually reproduce, i.e. below fs/2
          const freqs = [
            fc,
            fc * Math.pow(2, 0.1), fc / Math.pow(2, 0.1),
            fc * 2, fc / 2,
            fc * 8, fc / 8,
            20, 20000, NYQUIST * 0.999,
          ].filter((f) => f < NYQUIST)
          for (const f of freqs) {
            const actual = peakingBellDb(f, fc, gain, q)
            const expected = referencePeakingDb(f, fc, gain, q)
            expect(actual, `f=${f} fc=${fc} gain=${gain} q=${q}`).toBeCloseTo(expected, 8)
          }
        }
      }
    }
  })

  it('reaches exactly the specified gain at the center frequency', () => {
    // The bilinear transform is exact at the prewarped center
    for (const fc of CENTERS) {
      for (const gain of GAINS) {
        for (const q of QS) {
          expect(peakingBellDb(fc, fc, gain, q)).toBeCloseTo(gain, 10)
        }
      }
    }
  })

  it('is exactly flat at and above Nyquist', () => {
    for (const fc of CENTERS) {
      for (const q of QS) {
        expect(peakingBellDb(NYQUIST, fc, 12, q)).toBe(0)
        expect(peakingBellDb(NYQUIST * 1.5, fc, 12, q)).toBe(0)
      }
    }
    // ...and gets there smoothly: just short of fs/2 a 20 kHz Q1 bell that
    // the textbook shape would still hold at +5.8 dB has all but vanished
    expect(Math.abs(peakingBellDb(NYQUIST * 0.9999, 20000, 6, 1))).toBeLessThan(0.1)
  })

  it('agrees with the analog prototype where warping is negligible', () => {
    // Below a few kHz the drawn curve is also the textbook bell
    for (const fc of [50, 250, 1000]) {
      for (const q of QS) {
        for (const f of [fc / 4, fc / 1.2, fc, fc * 1.2, fc * 2]) {
          const err = Math.abs(peakingBellDb(f, fc, 6, q) - analogPeakingDb(f, fc, 6, q))
          expect(err, `f=${f} fc=${fc} q=${q}`).toBeLessThan(0.05)
        }
      }
    }
  })

  it('is squeezed toward Nyquist as the center rises, more on the high side', () => {
    // The same octave-fraction above the center gets less of the boost than
    // below it once the center is up where the warp bites...
    const r = Math.pow(2, 1 / 6)
    for (const fc of [8000, 12000, 16000]) {
      const above = peakingBellDb(fc * r, fc, 6, 2)
      const below = peakingBellDb(fc / r, fc, 6, 2)
      expect(above, `fc=${fc}`).toBeLessThan(below)
    }
    // ...but stays log-symmetric to within a hair well below Nyquist
    for (const ratio of [1.5, 2, 4]) {
      const above = peakingBellDb(200 * ratio, 200, 6, 1.4)
      const below = peakingBellDb(200 / ratio, 200, 6, 1.4)
      expect(above).toBeCloseTo(below, 2)
    }
  })

  it('takes an explicit sample rate', () => {
    // At 96 kHz the same 15 kHz bell sits much closer to the textbook shape
    const f = 15000 * Math.pow(2, 1 / 6)
    const errAt = (fs) => Math.abs(peakingBellDb(f, 15000, 6, 4, fs) - analogPeakingDb(f, 15000, 6, 4))
    expect(errAt(96000)).toBeLessThan(errAt(44100) / 4)
  })

  it('caps the center frequency the way the device does', () => {
    // A 30 kHz center can't run; the device runs 0.49*fs, so that is drawn
    for (const f of [1000, 10000, 20000]) {
      expect(peakingBellDb(f, 30000, 6, 1)).toBe(peakingBellDb(f, 0.49 * DEVICE_SAMPLE_RATE, 6, 1))
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

  it('never places two bands within 1/3 octave of each other', () => {
    // A sharp notch flanked by boosts: the shape that used to make the
    // greedy loop chase its own subtraction artifacts and stack duplicate
    // bands at one center.
    const correction = freqs.map((f) => {
      if (f > 300 && f < 380) return -12
      if (f > 200 && f < 500) return 6
      return 0
    })
    const points = fitPeqPoints(freqs, correction)
    for (let i = 0; i < points.length; i++) {
      for (let j = i + 1; j < points.length; j++) {
        const spacing = Math.abs(Math.log2(points[i].freq / points[j].freq))
        expect(spacing, `${points[i].freq} Hz vs ${points[j].freq} Hz`)
          .toBeGreaterThanOrEqual(1 / 3 - 0.01)
      }
    }
  })

  it('bounds each individual band by boostLimit/cutLimit', () => {
    // Alternating-sign correction at adjacent centers: strongly coupled
    // neighbors that full-step refinement used to drive to opposite clamps.
    const correction = freqs.map((f, i) =>
      f > 100 && f < 2000 ? (i % 2 ? 6 : -12) : 0
    )
    const points = fitPeqPoints(freqs, correction, { boostLimit: 6, cutLimit: 12 })
    expect(points.length).toBeGreaterThanOrEqual(1)
    for (const p of points) {
      expect(p.gain).toBeLessThanOrEqual(6)
      expect(p.gain).toBeGreaterThanOrEqual(-12)
    }
  })

  it('scales the width estimate with the grid resolution', () => {
    // The same one-octave bell sampled on a 1/12-octave grid: with the
    // grid resolution passed in, the fitted Q must stay near the true Q
    // instead of coming out ~4x too wide.
    const fine = Array.from({ length: 121 }, (_, i) => 20 * Math.pow(2, i / 12))
    const target = { freq: 1000, gain: 6, q: octavesToQ(1) }
    const correction = fine.map((f) => peakingBellDb(f, target.freq, target.gain, target.q))

    const points = fitPeqPoints(fine, correction, { bandsPerOctave: 12 })
    expect(points.length).toBeGreaterThanOrEqual(1)
    const main = points.reduce((a, b) => (Math.abs(b.gain) > Math.abs(a.gain) ? b : a))
    expect(main.q).toBeGreaterThan(target.q / 2)
    expect(main.q).toBeLessThan(target.q * 2)

    for (let i = 0; i < fine.length; i++) {
      const fitted = peqSumDb(points, fine[i])
      expect(Math.abs(fitted - correction[i]), `at ${fine[i].toFixed(0)} Hz`).toBeLessThan(1.5)
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

// Recognising our own write coming back is what lets an EQ apply whose reply
// was lost report the truth instead of a failure, so the comparison has to be
// exact about which bands it accepts.
describe('peqPointsMatch', () => {
  const wanted = [
    { freq: 112.2, gain: 6, q: 4.32 },
    { freq: 8414, gain: -3.7, q: 4.32 },
  ]

  it('matches a round-tripped copy of the same bands', () => {
    const stored = wanted.map((p) => ({ ...p, id: 0 }))
    expect(peqPointsMatch(stored, wanted)).toBe(true)
  })

  it('absorbs float round-tripping but not a rounding step', () => {
    expect(peqPointsMatch(
      [{ freq: 112.20000076, gain: 5.99999988, q: 4.3200001 }, wanted[1]],
      wanted,
    )).toBe(true)
    // 0.1 dB is a whole step of the generator's output - a different fit.
    expect(peqPointsMatch(
      [{ freq: 112.2, gain: 6.1, q: 4.32 }, wanted[1]],
      wanted,
    )).toBe(false)
  })

  it('rejects a different band count, order, or missing array', () => {
    expect(peqPointsMatch([wanted[0]], wanted)).toBe(false)
    expect(peqPointsMatch([wanted[1], wanted[0]], wanted)).toBe(false)
    expect(peqPointsMatch(undefined, wanted)).toBe(false)
    expect(peqPointsMatch(null, wanted)).toBe(false)
  })

  it('matches empty against empty - clearing the bands is a real outcome', () => {
    expect(peqPointsMatch([], [])).toBe(true)
  })
})
