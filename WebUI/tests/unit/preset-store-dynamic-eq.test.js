// Dynamic EQ in the preset store (docs/DYNAMIC_EQ.md). The invariant under
// test is the one the device also enforces: the loud anchor shares the
// reference anchor's band list and differs only in gain. The store mirrors
// it locally because edits are optimistic - a refetch mid-drag would fight
// the editor, so the local copy has to end up where the device does.

import { describe, it, expect, beforeEach, vi } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { usePresetStore } from '../../src/stores/preset.js'

vi.mock('../../src/api-client.js', () => ({
  default: {
    savePrefEqSet: vi.fn().mockResolvedValue(undefined),
    setInputEqLoudGains: vi.fn().mockResolvedValue(undefined),
    setInputEqAnchors: vi.fn().mockResolvedValue(undefined),
    clearInputEqLoud: vi.fn().mockResolvedValue(undefined),
    setLoudness: vi.fn().mockResolvedValue(undefined),
    getPreset: vi.fn().mockResolvedValue(null),
  },
}))

const PRESET = 'Living room'

const referencePoints = () => [
  { freq: 60, gain: 4, q: 0.7 },
  { freq: 800, gain: -3, q: 2 },
]

function seedStore({ loudGains = null, loudVolume = 0 } = {}) {
  const store = usePresetStore()
  const sets = [{ spl: 0, points: referencePoints() }]
  if (loudGains) {
    sets.push({
      spl: 1,
      points: referencePoints().map((p, i) => ({ ...p, gain: loudGains[i] })),
    })
  }
  store.presetName = PRESET
  store.preset = {
    name: PRESET,
    outputs: [],
    crossovers: [],
    volume: 50,
    inputEq: { enabled: true, sets, referenceVolume: 40, loudVolume, loudness: true },
  }
  return store
}

describe('preset store: dynamic EQ getters', () => {
  beforeEach(() => setActivePinia(createPinia()))

  it('reads the reference bands and the loud gains off their role tags', () => {
    const store = seedStore({ loudGains: [9, -7], loudVolume: 85 })
    expect(store.inputEqPoints.map((p) => p.freq)).toEqual([60, 800])
    expect(store.loudGains).toEqual([9, -7])
    expect(store.referenceVolume).toBe(40)
    expect(store.loudVolume).toBe(85)
    expect(store.hasLoudAnchor).toBe(true)
    expect(store.loudness).toBe(true)
  })

  it('reports no loud anchor when the anchor volume is 0', () => {
    const store = seedStore()
    expect(store.loudGains).toEqual([])
    expect(store.hasLoudAnchor).toBe(false)
  })
})

describe('preset store: loud anchor follows the reference bands', () => {
  beforeEach(() => setActivePinia(createPinia()))

  it('mirrors a band move and keeps the loud gains', async () => {
    const store = seedStore({ loudGains: [9, -7], loudVolume: 85 })
    await store.saveInputEq([
      { freq: 45, gain: 4, q: 0.5 },
      { freq: 1200, gain: -2, q: 4 },
    ])
    const loud = store.preset.inputEq.sets.find((s) => s.spl === 1)
    expect(loud.points.map((p) => p.freq)).toEqual([45, 1200])
    expect(loud.points.map((p) => p.q)).toEqual([0.5, 4])
    expect(loud.points.map((p) => p.gain)).toEqual([9, -7])
  })

  it('starts an added band flat and drops removed ones', async () => {
    const store = seedStore({ loudGains: [9, -7], loudVolume: 85 })
    await store.saveInputEq([...referencePoints(), { freq: 9000, gain: 1, q: 3 }])
    expect(store.loudGains).toEqual([9, -7, 0])

    await store.saveInputEq([referencePoints()[0]])
    expect(store.loudGains).toEqual([9])
  })

  it('creates the loud set from the reference bands on the first write', async () => {
    const store = seedStore()
    await store.saveLoudGains([6, -6])
    const loud = store.preset.inputEq.sets.find((s) => s.spl === 1)
    expect(loud.points.map((p) => p.freq)).toEqual([60, 800])
    expect(loud.points.map((p) => p.gain)).toEqual([6, -6])
  })

  it('stores a loud anchor at or below the reference as none', async () => {
    const store = seedStore({ loudGains: [9, -7], loudVolume: 85 })
    await store.setEqAnchors(40, 30)
    expect(store.loudVolume).toBe(0)
    expect(store.hasLoudAnchor).toBe(false)

    await store.setEqAnchors(40, 90)
    expect(store.loudVolume).toBe(90)
  })

  it('clears the set as well as the anchor when the loud curve is removed', async () => {
    const store = seedStore({ loudGains: [9, -7], loudVolume: 85 })
    await store.clearLoudAnchor()
    expect(store.preset.inputEq.sets.some((s) => s.spl === 1)).toBe(false)
    expect(store.loudVolume).toBe(0)
    // The reference curve survives
    expect(store.inputEqPoints).toHaveLength(2)
  })
})

describe('preset store: dynamic EQ live updates', () => {
  beforeEach(() => setActivePinia(createPinia()))

  it('merges anchor and loudness broadcasts', () => {
    const store = seedStore()
    store.handleLiveMessage({
      messageType: 'eqAnchorsChanged', presetName: PRESET, referenceVolume: 35, loudVolume: 90,
    })
    expect(store.referenceVolume).toBe(35)
    expect(store.loudVolume).toBe(90)

    store.handleLiveMessage({
      messageType: 'eqLoudnessChanged', presetName: PRESET, loudness: false,
    })
    expect(store.loudness).toBe(false)
  })

  it('rebuilds the loud set from the broadcast gains and the local bands', () => {
    const store = seedStore()
    store.handleLiveMessage({
      messageType: 'eqLoudChanged', presetName: PRESET, loudVolume: 90, gains: [5, -5],
    })
    const loud = store.preset.inputEq.sets.find((s) => s.spl === 1)
    expect(loud.points.map((p) => p.freq)).toEqual([60, 800])
    expect(loud.points.map((p) => p.gain)).toEqual([5, -5])
  })

  it('treats an empty gains list as the anchor having been removed', () => {
    const store = seedStore({ loudGains: [9, -7], loudVolume: 85 })
    store.handleLiveMessage({
      messageType: 'eqLoudChanged', presetName: PRESET, loudVolume: 0, gains: [],
    })
    expect(store.preset.inputEq.sets.some((s) => s.spl === 1)).toBe(false)
    expect(store.hasLoudAnchor).toBe(false)
  })

  it('ignores broadcasts about another preset', () => {
    const store = seedStore()
    store.handleLiveMessage({
      messageType: 'eqAnchorsChanged', presetName: 'Kitchen', referenceVolume: 10, loudVolume: 20,
    })
    expect(store.referenceVolume).toBe(40)
  })
})
