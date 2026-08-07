import { describe, it, expect } from 'vitest'
import { averageDbArrays, BUILTIN_CAL_PRESETS, makeBandGrid, aggregateBands } from '../../src/rta.js'

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
