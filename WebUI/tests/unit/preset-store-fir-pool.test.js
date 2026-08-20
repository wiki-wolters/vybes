// FIR pool live-update merging. Failures arrive one firLoadError per failed
// channel and are merged into the store's copy, so on their own they can only
// ever add: a filter that started loading again kept its banner, and errors
// from separate loads accumulated into a set that never coexisted on the
// device. firPoolChanged is what clears the slate.

import { describe, it, expect, beforeEach } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { usePresetStore } from '../../src/stores/preset.js'

const PRESET = 'Desk FIR'

// Minimal preset shape: handleLiveMessage only needs firPool to exist and the
// name to match, but outputs/crossovers/inputEq keep the getters happy.
function seedStore() {
  const store = usePresetStore()
  store.presetName = PRESET
  store.preset = {
    name: PRESET,
    outputs: [],
    crossovers: [],
    inputEq: { sets: [] },
    firPool: { total: 12288, used: 12288, errors: [] },
  }
  return store
}

const loadStarting = (errors = []) => ({
  messageType: 'firPoolChanged',
  presetName: PRESET,
  firPool: { total: 12288, used: 12288, errors },
})

const failure = (output, file) => ({
  messageType: 'firLoadError',
  presetName: PRESET,
  output,
  code: 'nomem',
  file,
})

describe('preset store: FIR pool live updates', () => {
  beforeEach(() => setActivePinia(createPinia()))

  it('records a failure reported during a load', () => {
    const store = seedStore()
    store.handleLiveMessage(failure(3, 'S-correction.wav'))

    expect(store.firPool.errors).toEqual([
      { output: 3, code: 'nomem', file: 'S-correction.wav' },
    ])
  })

  it('clears the banner when a load starts clean', () => {
    const store = seedStore()
    store.handleLiveMessage(failure(3, 'S-correction.wav'))
    store.handleLiveMessage(loadStarting())

    expect(store.firPool.errors).toEqual([])
  })

  it('does not accumulate failures across separate loads', () => {
    const store = seedStore()

    // A load where the sub failed
    store.handleLiveMessage(loadStarting())
    store.handleLiveMessage(failure(3, 'S-correction.wav'))
    expect(store.firPool.errors).toHaveLength(1)

    // A later load where the sub loaded and the right channel failed instead.
    // Without the reset these two would stack into a pair of failures that
    // never happened together - the bug this covers.
    store.handleLiveMessage(loadStarting())
    store.handleLiveMessage(failure(1, 'R-correction.wav'))

    expect(store.firPool.errors).toEqual([
      { output: 1, code: 'nomem', file: 'R-correction.wav' },
    ])
  })

  it('keeps every failure within one load', () => {
    const store = seedStore()
    store.handleLiveMessage(loadStarting())
    store.handleLiveMessage(failure(1, 'R-correction.wav'))
    store.handleLiveMessage(failure(3, 'S-correction.wav'))

    expect(store.firPool.errors.map((e) => e.output)).toEqual([1, 3])
  })

  it('takes capacity and usage from the load-starting message', () => {
    const store = seedStore()
    store.handleLiveMessage({
      messageType: 'firPoolChanged',
      presetName: PRESET,
      firPool: { total: 12288, used: 9216, errors: [] },
    })

    expect(store.firPool).toMatchObject({ total: 12288, used: 9216 })
  })

  it('ignores a load for a preset the editor is not showing', () => {
    const store = seedStore()
    store.handleLiveMessage(failure(3, 'S-correction.wav'))
    store.handleLiveMessage({ ...loadStarting(), presetName: 'Some Other Preset' })

    expect(store.firPool.errors).toHaveLength(1)
  })
})
