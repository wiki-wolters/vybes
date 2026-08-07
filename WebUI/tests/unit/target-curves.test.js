import { describe, it, expect } from 'vitest'
import { TARGET_CURVE_PRESETS, targetCurveForGrid } from '../../src/target-curves.js'
import { makeBandGrid } from '../../src/rta.js'

const grid = makeBandGrid(3)

function median(values) {
  const arr = [...values].sort((a, b) => a - b)
  const mid = Math.floor(arr.length / 2)
  return arr.length % 2 ? arr[mid] : (arr[mid - 1] + arr[mid]) / 2
}

describe('targetCurveForGrid', () => {
  it('re-centers so the median over the alignment window is 0', () => {
    for (const preset of TARGET_CURVE_PRESETS) {
      const curve = targetCurveForGrid(preset.points, grid)
      const inWindow = curve.filter(
        (v, i) => grid.centers[i] >= 200 && grid.centers[i] <= 5000
      )
      expect(median(inWindow), preset.id).toBeCloseTo(0, 10)
    }
  })

  it('an all-zero curve stays zero everywhere', () => {
    const curve = targetCurveForGrid([[20, 0], [20000, 0]], grid)
    for (const v of curve) expect(v).toBeCloseTo(0, 10)
  })

  it('re-centering removes any constant offset', () => {
    const base = targetCurveForGrid([[20, 3], [400, 1], [20000, -4]], grid)
    const shifted = targetCurveForGrid([[20, 13], [400, 11], [20000, 6]], grid)
    for (let i = 0; i < base.length; i++) expect(shifted[i]).toBeCloseTo(base[i], 10)
  })

  it('returns one value per grid band', () => {
    for (const preset of TARGET_CURVE_PRESETS) {
      expect(targetCurveForGrid(preset.points, grid).length).toBe(grid.centers.length)
    }
  })
})

describe('preset shapes', () => {
  const at = (curve, freq) => {
    let best = 0
    for (let i = 1; i < grid.centers.length; i++) {
      if (Math.abs(grid.centers[i] - freq) < Math.abs(grid.centers[best] - freq)) best = i
    }
    return curve[best]
  }

  it('Harman: bass shelf above the mids, treble below them', () => {
    const preset = TARGET_CURVE_PRESETS.find((c) => c.id === 'harman')
    const curve = targetCurveForGrid(preset.points, grid)
    expect(at(curve, 25) - at(curve, 1000)).toBeGreaterThan(5)
    expect(at(curve, 16000) - at(curve, 1000)).toBeLessThan(-3)
    // Monotonically non-rising from bass to treble
    for (let i = 1; i < curve.length; i++) {
      expect(curve[i]).toBeLessThanOrEqual(curve[i - 1] + 1e-9)
    }
  })

  it('B&K: flat through 400 Hz, then -1 dB/octave', () => {
    const preset = TARGET_CURVE_PRESETS.find((c) => c.id === 'bk')
    const curve = targetCurveForGrid(preset.points, grid)
    expect(at(curve, 25)).toBeCloseTo(at(curve, 315), 1)
    // One octave apart in the sloped region differs by ~1 dB
    expect(at(curve, 2000) - at(curve, 4000)).toBeCloseTo(1, 1)
    expect(at(curve, 4000) - at(curve, 8000)).toBeCloseTo(1, 1)
  })
})
