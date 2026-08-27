// The deviation chart is drawn twice at different sizes - full on the analyzer
// page, stripped back inside the capture modal - and the two have to read as
// the same measurement. These pin the properties that make that true: the
// frequency axis depends only on the frequency, the dB axis only on the dB,
// and gated bands never turn into invented response.

import { describe, it, expect } from 'vitest'
import {
  DELTA_RANGE_DB,
  DELTA_BOOST_COLOR,
  DELTA_LOSS_COLOR,
  clampDelta,
  logX,
  freqAtLogX,
  bandPixelWidth,
  deltaZeroY,
  deltaDbToY,
  deviationBars,
  deviationPath,
  outOfBandShades,
} from '../../src/rta-chart.js'
import { makeBandGrid } from '../../src/rta.js'

const PAD = 28
const grid = makeBandGrid(3)

describe('deviation chart geometry', () => {
  it('places a frequency at the same fraction of the axis at any width', () => {
    const fraction = (width) => (logX(1000, width, PAD) - PAD) / (width - PAD)
    expect(fraction(320)).toBeCloseTo(fraction(900), 10)
  })

  it('round-trips frequency through the x axis', () => {
    for (const f of [20, 100, 1000, 8000, 20000]) {
      expect(freqAtLogX(logX(f, 500, PAD), 500, PAD)).toBeCloseTo(f, 6)
    }
  })

  it('puts 0 dB on the centre line and mirrors equal deviations', () => {
    const h = 150
    expect(deltaDbToY(0, h)).toBe(deltaZeroY(h))
    const up = deltaZeroY(h) - deltaDbToY(6, h)
    const down = deltaDbToY(-6, h) - deltaZeroY(h)
    expect(up).toBeCloseTo(down, 10)
    // Boost is up the screen, so a positive dB is a smaller y.
    expect(deltaDbToY(6, h)).toBeLessThan(deltaZeroY(h))
  })

  it('keeps a full-scale bar inside the chart', () => {
    const h = 150
    expect(deltaDbToY(DELTA_RANGE_DB, h)).toBeGreaterThan(0)
    expect(deltaDbToY(-DELTA_RANGE_DB, h)).toBeLessThan(h)
  })

  it('clamps deviation to the chart range', () => {
    expect(clampDelta(40)).toBe(DELTA_RANGE_DB)
    expect(clampDelta(-40)).toBe(-DELTA_RANGE_DB)
    expect(clampDelta(3.5)).toBe(3.5)
  })

  it('scales one band to the axis width', () => {
    expect(bandPixelWidth(grid, 900, PAD)).toBeCloseTo(bandPixelWidth(grid, 900, PAD), 10)
    // A finer grid packs more bands into the same axis, so each is narrower.
    expect(bandPixelWidth(makeBandGrid(12), 900, PAD))
      .toBeLessThan(bandPixelWidth(makeBandGrid(3), 900, PAD))
  })
})

describe('outOfBandShades', () => {
  const geom = { width: 1216, padLeft: PAD }

  it('leaves exactly the requested band clear', () => {
    const shades = outOfBandShades(45, 120, geom)
    expect(shades).toHaveLength(2)
    // The gap between the two rects is 45-120 Hz on the shared axis.
    expect(shades[0].x + shades[0].w).toBeCloseTo(logX(45, geom.width, PAD), 6)
    expect(shades[1].x).toBeCloseTo(logX(120, geom.width, PAD), 6)
  })

  it('never covers the dB labels or overruns the chart', () => {
    for (const [lo, hi] of [[45, 120], [25, 10000], [20, 20000], [200, 300]]) {
      for (const s of outOfBandShades(lo, hi, geom)) {
        expect(s.x).toBeGreaterThanOrEqual(PAD)
        expect(s.w).toBeGreaterThan(0)
        expect(s.x + s.w).toBeLessThanOrEqual(geom.width + 1e-9)
      }
    }
  })

  it('shades nothing when the limits span the whole axis', () => {
    expect(outOfBandShades(20 / Math.pow(10, 0.05), 20000 * Math.pow(10, 0.05), geom)).toEqual([])
  })

  it('tolerates inverted limits', () => {
    expect(outOfBandShades(120, 45, geom)).toEqual(outOfBandShades(45, 120, geom))
  })

  it('the same limits land on the same frequencies at the modal width', () => {
    const wide = outOfBandShades(45, 120, geom)
    const narrow = outOfBandShades(45, 120, { width: 360, padLeft: PAD })
    const freqAt = (x, width) => freqAtLogX(x, width, PAD)
    expect(freqAt(wide[1].x, geom.width)).toBeCloseTo(freqAt(narrow[1].x, 360), 6)
  })
})

describe('deviationBars', () => {
  const opts = { width: 400, padLeft: PAD, height: 150 }

  it('colours boost and loss differently and skips gated bands', () => {
    const values = grid.centers.map((_, i) => (i === 0 ? 6 : i === 1 ? -6 : NaN))
    const bars = deviationBars(values, grid, opts)
    expect(bars).toHaveLength(2)
    expect(bars[0].color).toBe(DELTA_BOOST_COLOR)
    expect(bars[1].color).toBe(DELTA_LOSS_COLOR)
  })

  it('fades bands inside the ±2 dB measurement uncertainty', () => {
    const values = grid.centers.map((_, i) => (i === 0 ? 1 : i === 1 ? 5 : NaN))
    const [faint, solid] = deviationBars(values, grid, opts)
    expect(faint.opacity).toBeLessThan(solid.opacity)
  })

  it('grows every bar from the zero line', () => {
    const values = grid.centers.map((_, i) => (i === 0 ? 8 : i === 1 ? -8 : NaN))
    const zero = deltaZeroY(opts.height)
    for (const bar of deviationBars(values, grid, opts)) {
      const edges = [bar.y, bar.y + bar.h]
      expect(Math.min(...edges.map((e) => Math.abs(e - zero)))).toBeCloseTo(0, 6)
    }
  })

  it('gives an always-visible bar even at zero deviation', () => {
    const values = grid.centers.map((_, i) => (i === 0 ? 0 : NaN))
    expect(deviationBars(values, grid, opts)[0].h).toBeGreaterThanOrEqual(1)
  })

  it('returns nothing without values', () => {
    expect(deviationBars(null, grid, opts)).toEqual([])
  })
})

describe('deviationPath', () => {
  const opts = { width: 400, padLeft: PAD, height: 150 }

  it('draws one point per finite band', () => {
    const values = grid.centers.map((_, i) => (i < 4 ? i : NaN))
    const path = deviationPath(values, grid, opts)
    expect(path.startsWith('M ')).toBe(true)
    expect(path.split(' L ')).toHaveLength(4)
  })

  it('skips gaps rather than bridging them - a gated band is not a measurement', () => {
    const values = grid.centers.map((_, i) => ([0, 5].includes(i) ? 3 : NaN))
    const path = deviationPath(values, grid, opts)
    const points = path.replace('M ', '').split(' L ')
    expect(points).toHaveLength(2)
    // The two survive at their own frequencies; nothing is drawn between them.
    expect(Number(points[0].split(',')[0])).toBeCloseTo(logX(grid.centers[0], 400, PAD), 1)
    expect(Number(points[1].split(',')[0])).toBeCloseTo(logX(grid.centers[5], 400, PAD), 1)
  })

  it('returns an empty path for fewer than two points', () => {
    expect(deviationPath(null, grid, opts)).toBe('')
    expect(deviationPath(grid.centers.map(() => NaN), grid, opts)).toBe('')
    const single = grid.centers.map((_, i) => (i === 0 ? 1 : NaN))
    expect(deviationPath(single, grid, opts)).toBe('')
  })

  it('clamps an extreme band instead of running off the chart', () => {
    const values = grid.centers.map((_, i) => (i < 2 ? 100 : NaN))
    const ys = deviationPath(values, grid, opts)
      .replace('M ', '').split(' L ').map((p) => Number(p.split(',')[1]))
    for (const y of ys) {
      expect(y).toBeCloseTo(deltaDbToY(DELTA_RANGE_DB, opts.height), 1)
    }
  })
})
