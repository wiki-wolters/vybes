import { describe, it, expect } from 'vitest'
import { averageDbArrays, BUILTIN_CAL_PRESETS, calCurveForGrid, makeBandGrid, aggregateBands, medianOffset, makePeakHold, updatePeakHold } from '../../src/rta.js'

describe('averageDbArrays', () => {
  it('returns null for an empty list', () => {
    expect(averageDbArrays([])).toBeNull()
  })

  it('returns the input values for a single array', () => {
    const a = [0, -6, 3.5, -80]
    const out = averageDbArrays([a])
    for (let i = 0; i < a.length; i++) expect(out[i]).toBeCloseTo(a[i], 10)
  })

  it('averages in the power domain, not in dB', () => {
    // 0 dB and -10 dB: power mean = (1 + 0.1) / 2 = 0.55 -> -2.596 dB,
    // clearly different from the dB mean of -5.
    const out = averageDbArrays([[0], [-10]])
    expect(out[0]).toBeCloseTo(10 * Math.log10(0.55), 10)
  })

  it('identical arrays average to themselves', () => {
    const a = [-3, 2, 7]
    const out = averageDbArrays([a, [...a], [...a]])
    for (let i = 0; i < a.length; i++) expect(out[i]).toBeCloseTo(a[i], 10)
  })

  it('skips NaN entries per band and averages the finite ones', () => {
    const out = averageDbArrays([
      [0, NaN, NaN],
      [-10, 5, NaN],
    ])
    expect(out[0]).toBeCloseTo(10 * Math.log10(0.55), 10) // both finite
    expect(out[1]).toBeCloseTo(5, 10) // only the second array has a value
    expect(Number.isNaN(out[2])).toBe(true) // nobody has a value
  })

  it('the average lies between the extremes', () => {
    const out = averageDbArrays([[-8], [-2], [4]])
    expect(out[0]).toBeGreaterThan(-8)
    expect(out[0]).toBeLessThan(4)
  })
})

describe('BUILTIN_CAL_PRESETS', () => {
  const preset = BUILTIN_CAL_PRESETS.find((p) => p.id === 'smartphone-hpf')

  it('exists and has sorted [freq, gain] points', () => {
    expect(preset).toBeDefined()
    expect(preset.points.length).toBeGreaterThanOrEqual(2)
    for (let i = 1; i < preset.points.length; i++) {
      expect(preset.points[i][0]).toBeGreaterThan(preset.points[i - 1][0])
    }
  })

  it('models a high-pass: strongly negative at 20 Hz, ~-3 dB near the corner, ~0 up high', () => {
    const at = (f) => preset.points.find((p) => p[0] === f)[1]
    expect(at(20)).toBeLessThan(-15)
    expect(at(50)).toBeGreaterThan(-5)
    expect(at(50)).toBeLessThan(-2.5)
    expect(Math.abs(at(20000))).toBeLessThan(0.2)
  })

  it('deviation is monotonically rising with frequency (a plain roll-off)', () => {
    for (let i = 1; i < preset.points.length; i++) {
      expect(preset.points[i][1]).toBeGreaterThanOrEqual(preset.points[i - 1][1])
    }
  })
})

describe('iPhone 17 Pro cal preset', () => {
  const preset = BUILTIN_CAL_PRESETS.find((p) => p.id === 'iphone-17-pro')
  const at = (f) => preset.points.find((p) => p[0] === f)[1]

  it('exists and has sorted [freq, gain] points spanning 20Hz-20kHz', () => {
    expect(preset).toBeDefined()
    expect(preset.points[0][0]).toBeLessThanOrEqual(20)
    expect(preset.points[preset.points.length - 1][0]).toBeGreaterThanOrEqual(20000)
    for (let i = 1; i < preset.points.length; i++) {
      expect(preset.points[i][0]).toBeGreaterThan(preset.points[i - 1][0])
    }
  })

  it('is flat across the level-alignment window', () => {
    // 200Hz-5kHz is where medianOffset aligns the mic to the source. A bump
    // in here would bias every band outside it, so it has to stay small.
    for (const f of [200, 500, 1000, 2500, 5000]) expect(Math.abs(at(f))).toBeLessThan(1.5)
  })

  it('rolls off in the bottom octave', () => {
    expect(at(20)).toBeLessThan(-10)
    expect(at(50)).toBeLessThan(-3)
    expect(at(50)).toBeGreaterThan(-5)
    expect(at(100)).toBeGreaterThan(-2.5)
  })

  it('interpolates onto a band grid without gaps', () => {
    const curve = calCurveForGrid(preset.points, makeBandGrid(12))
    expect(curve).toHaveLength(121)
    expect(curve.every((v) => Number.isFinite(v))).toBe(true)
  })
})

describe('medianOffset', () => {
  const { centers } = makeBandGrid(3)

  it('returns the median level difference over the window', () => {
    const b = centers.map(() => -30)
    const a = centers.map(() => -45) // a - b = -15 everywhere
    expect(medianOffset(a, b, centers, 200, 5000)).toBeCloseTo(-15, 6)
  })

  it('ignores bands outside [loHz, hiHz]', () => {
    const b = centers.map(() => -30)
    // -10 offset inside 200-5000 Hz; absurd values outside must not count
    const a = centers.map((fc) => (fc >= 200 && fc <= 5000 ? -40 : 40))
    expect(medianOffset(a, b, centers, 200, 5000)).toBeCloseTo(-10, 6)
  })

  it('aligns a band-limited output inside its passband, not on dead bands', () => {
    // Sub: real signal only 40-80 Hz (mic - source = -20); the full-range
    // source is present everywhere, but the mic reads its noise floor above.
    const b = centers.map(() => -30)
    const a = centers.map((fc) => (fc >= 40 && fc <= 80 ? -50 : -88))
    // Aligned on the sub's passband -> the true -20 dB offset.
    expect(medianOffset(a, b, centers, 40, 80)).toBeCloseTo(-20, 6)
    // Aligned on the old 200-5000 Hz window -> pure noise-floor arithmetic
    // (this was the sub bug: a wildly wrong offset the whole curve inherits).
    expect(medianOffset(a, b, centers, 200, 5000)).toBeCloseTo(-58, 6)
  })

  it('drops bands where the mic sits within the margin of its noise floor', () => {
    // Same sub, but isolated by the floor gate instead of a narrow window:
    // dead bands sit above the -95 hard gate yet within 8 dB of the floor.
    const b = centers.map(() => -30)
    const floor = centers.map(() => -90)
    const a = centers.map((fc) => (fc >= 40 && fc <= 80 ? -50 : -88))
    expect(medianOffset(a, b, centers, 20, 20000, floor, 8)).toBeCloseTo(-20, 6)
    // Without the gate the noise-floor bands dominate and wreck the offset.
    expect(medianOffset(a, b, centers, 20, 20000)).toBeCloseTo(-58, 6)
  })
})

describe('aggregateBands with a noise-floor-shaped input', () => {
  it('conserves total power going from the 1/12 grid to the 1/3 grid', () => {
    const fine = makeBandGrid(12)
    const coarse = makeBandGrid(3)
    const flat = new Float32Array(fine.centers.length).fill(-60)
    const out = aggregateBands(flat, fine, coarse)
    // Four 1/12-octave bands sum into one 1/3-octave band: +6 dB
    for (let i = 1; i < out.length - 1; i++) {
      expect(out[i]).toBeCloseTo(-60 + 10 * Math.log10(4), 1)
    }
  })
})

describe('updatePeakHold', () => {
  const DECAY = 15 // dB/s
  const HOLD = 800 // ms

  // Drive one band with a constant level for a stretch of 100ms frames.
  const run = (state, level, frames, startMs) => {
    let t = startMs
    for (let i = 0; i < frames; i++) {
      t += 100
      updatePeakHold(state, [level], t, 100, DECAY, HOLD)
    }
    return t
  }

  it('snaps up to the first frame from the initial floor', () => {
    const state = makePeakHold(1)
    expect(state.values[0]).toBe(-200)
    updatePeakHold(state, [-30], 1000, 100, DECAY, HOLD)
    expect(state.values[0]).toBeCloseTo(-30, 6)
  })

  it('holds the peak flat for holdMs after the level drops', () => {
    const state = makePeakHold(1)
    let t = run(state, -30, 1, 0)
    // 700ms of a much quieter signal: still inside the hold window
    t = run(state, -60, 7, t)
    expect(state.values[0]).toBeCloseTo(-30, 6)
  })

  it('falls at decayDbPerSec once the hold expires', () => {
    const state = makePeakHold(1)
    let t = run(state, -30, 1, 0)
    t = run(state, -60, 8, t) // hold expires on the 8th frame (800ms)
    const afterHold = state.values[0]
    expect(afterHold).toBeCloseTo(-31.5, 6) // one frame's decay: 15 dB/s * 0.1s
    t = run(state, -60, 10, t)
    expect(state.values[0]).toBeCloseTo(afterHold - 15, 6) // a further second
  })

  it('never falls below the current level', () => {
    const state = makePeakHold(1)
    let t = run(state, -30, 1, 0)
    run(state, -40, 40, t) // 4s, far more decay than the 10 dB gap
    expect(state.values[0]).toBeCloseTo(-40, 6)
  })

  it('re-arms the hold whenever a band is pushed back up', () => {
    const state = makePeakHold(1)
    let t = run(state, -30, 1, 0)
    t = run(state, -60, 12, t) // decayed past the hold
    expect(state.values[0]).toBeLessThan(-30)
    t = run(state, -20, 1, t) // a louder frame
    expect(state.values[0]).toBeCloseTo(-20, 6)
    t = run(state, -60, 7, t) // pinned again for the full hold
    expect(state.values[0]).toBeCloseTo(-20, 6)
  })

  it('separates from a steady signal whose jitter is under one frame of decay', () => {
    // The reason the hold stage exists: with 1.5 dB of decay per frame and
    // jitter smaller than that, a hold-less peak would sit exactly on the
    // current level and show nothing.
    const state = makePeakHold(1)
    let t = 0
    let last = 0
    for (let i = 0; i < 60; i++) {
      t += 100
      last = -40 + (i % 4 === 0 ? 0.8 : -0.4) // ±~1 dB of band jitter
      updatePeakHold(state, [last], t, 100, DECAY, HOLD)
    }
    expect(state.values[0]).toBeGreaterThan(last + 0.5)
  })

  it('tracks each band independently', () => {
    const state = makePeakHold(3)
    updatePeakHold(state, [-10, -50, -30], 1000, 100, DECAY, HOLD)
    // A 1s frame: the 800ms hold has expired, so a full second of decay applies
    updatePeakHold(state, [-70, -50, -70], 2000, 1000, DECAY, HOLD)
    expect(state.values[0]).toBeCloseTo(-25, 6)
    expect(state.values[1]).toBeCloseTo(-50, 6) // re-armed by its own level, held
    expect(state.values[2]).toBeCloseTo(-45, 6)
  })
})
