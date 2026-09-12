import { describe, it, expect } from 'vitest'
import {
  TARGET_CURVE_PRESETS,
  DEFAULT_TARGET,
  targetCurveForGrid,
  targetDbOnFreqs,
} from '../../src/target-curves.js'
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

describe('targetDbOnFreqs', () => {
  // An arbitrary grid, nothing like the analyzer's band centers: what the
  // FIR wizard hands it is gatedResponse's log grid over one passband.
  const passbandFreqs = Float64Array.from(
    Array.from({ length: 97 }, (_, i) => 300 * Math.pow(2, i / 24))
  )

  it('tilt is 0 at the 1 kHz pivot and linear in octaves either side', () => {
    const freqs = [250, 500, 1000, 2000, 4000]
    const db = targetDbOnFreqs({ mode: 'tilt', tiltDbPerOct: -1 }, freqs)
    expect(db[2]).toBeCloseTo(0, 12)
    expect(db[1]).toBeCloseTo(1, 12)
    expect(db[0]).toBeCloseTo(2, 12)
    expect(db[3]).toBeCloseTo(-1, 12)
    expect(db[4]).toBeCloseTo(-2, 12)
  })

  it('tilt is not re-centered, whatever window it is given', () => {
    const spec = { mode: 'tilt', tiltDbPerOct: -0.5 }
    const wide = targetDbOnFreqs(spec, passbandFreqs)
    const narrow = targetDbOnFreqs(spec, passbandFreqs, { loHz: 4000, hiHz: 8000 })
    for (let i = 0; i < wide.length; i++) expect(narrow[i]).toBeCloseTo(wide[i], 12)
  })

  it('flat, an unknown mode, and custom with nothing imported are all zero', () => {
    for (const spec of [{ mode: 'flat' }, { mode: 'nonsense' }, { mode: 'custom' }, null]) {
      const db = targetDbOnFreqs(spec, passbandFreqs)
      expect(db.length).toBe(passbandFreqs.length)
      for (const v of db) expect(v).toBe(0)
    }
  })

  it('re-centers presets over the window it is given, on any grid', () => {
    for (const preset of TARGET_CURVE_PRESETS) {
      const db = targetDbOnFreqs({ mode: preset.id }, passbandFreqs, { loHz: 1000, hiHz: 4000 })
      const inWindow = [...db].filter(
        (v, i) => passbandFreqs[i] >= 1000 && passbandFreqs[i] <= 4000
      )
      expect(median(inWindow), preset.id).toBeCloseTo(0, 10)
      // The window only moves the curve, never its shape.
      const other = targetDbOnFreqs({ mode: preset.id }, passbandFreqs, { loHz: 300, hiHz: 600 })
      for (let i = 1; i < db.length; i++) {
        expect(other[i] - other[i - 1]).toBeCloseTo(db[i] - db[i - 1], 10)
      }
    }
  })

  it('interpolates custom points log-linearly, like a cal file', () => {
    // -6 dB per decade: the value 1 decade up is 6 dB down, and the
    // geometric midpoint sits exactly halfway.
    const db = targetDbOnFreqs(
      { mode: 'custom', points: [[100, 0], [1000, -6]] },
      [100, 316.227766, 1000],
      { loHz: 100, hiHz: 100 }
    )
    expect(db[0]).toBeCloseTo(0, 6)
    expect(db[1]).toBeCloseTo(-3, 4)
    expect(db[2]).toBeCloseTo(-6, 6)
  })

  it('accepts custom points under either key (customPoints is the stored one)', () => {
    const points = [[20, 4], [20000, -4]]
    const a = targetDbOnFreqs({ mode: 'custom', points }, passbandFreqs)
    const b = targetDbOnFreqs({ mode: 'custom', customPoints: points }, passbandFreqs)
    for (let i = 0; i < a.length; i++) expect(b[i]).toBeCloseTo(a[i], 12)
  })

  it('agrees with the analyzer on the compare grid, every mode', () => {
    const custom = [[20, 5], [200, 2], [2000, 0], [20000, -6]]
    const modes = ['flat', 'custom', ...TARGET_CURVE_PRESETS.map((c) => c.id)]
    for (const mode of modes) {
      // What AnalyzerView's targetGridCurve computes for this mode...
      const analyzer =
        mode === 'flat'
          ? grid.centers.map(() => 0)
          : targetCurveForGrid(
              mode === 'custom' ? custom : TARGET_CURVE_PRESETS.find((c) => c.id === mode).points,
              grid, 200, 5000
            )
      const shared = targetDbOnFreqs({ mode, customPoints: custom }, grid.centers, {
        loHz: 200, hiHz: 5000,
      })
      for (let i = 0; i < analyzer.length; i++) expect(shared[i], mode).toBeCloseTo(analyzer[i], 12)
    }
    // ...and the tilt mode, which the analyzer computes inline.
    const tilt = targetDbOnFreqs(DEFAULT_TARGET, grid.centers)
    for (let i = 0; i < grid.centers.length; i++) {
      expect(tilt[i]).toBeCloseTo(DEFAULT_TARGET.tiltDbPerOct * Math.log2(grid.centers[i] / 1000), 12)
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
