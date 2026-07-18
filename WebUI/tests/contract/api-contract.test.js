/*
 * Vybes HTTP + websocket API contract suite.
 *
 * The contract's source of truth is the ESP firmware
 * (ESP/esp-web-server/api_*.cpp + web_server.cpp). This suite asserts the
 * response/broadcast SHAPES and units of every endpoint the WebUI client
 * (src/api-client.js) uses, so it passes against both the mock server and a
 * real device.
 *
 * Modes:
 *  - `npm run test:contract`            hermetic: spawns mock-server/server.js
 *    on ephemeral ports with a throwaway sqlite DB (see harness.js).
 *  - `VYBES_API_URL=http://vybes.local npm run test:contract`   real device.
 *
 * Real-device politeness:
 *  - All preset mutations happen on uniquely named "contract-test-…" presets
 *    created by the suite and deleted in afterAll.
 *  - Global values the suite touches (volume, mute, mute percent, speaker
 *    gains, input gains, tone, noise, active preset) are snapshotted from
 *    /status in beforeAll and restored in afterAll.
 *  - Tone/noise tests run at volume 1 and are stopped immediately.
 *  - Destructive checks that can't be restored (deleting down to the last
 *    remaining preset, /restore) only run against the mock.
 *  - Error responses are asserted by status code only: the ESP replies
 *    text/plain where the mock replies JSON.
 */
import { describe, it, expect, beforeAll, afterAll } from 'vitest'
import { startTarget, connectWs, api, REAL_DEVICE } from './harness.js'

const itMockOnly = REAL_DEVICE ? it.skip : it
const enc = encodeURIComponent

const PREFIX = `contract-test-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 6)}`
const P = `${PREFIX}-main` // dedicated preset for all preset mutations

let t // target from harness
let snapshot // /status snapshot for restoring globals

const GET = (e) => api(t.baseUrl, 'GET', e)
const PUT = (e, b) => api(t.baseUrl, 'PUT', e, b)
const POST = (e, b) => api(t.baseUrl, 'POST', e, b)
const DEL = (e) => api(t.baseUrl, 'DELETE', e)

const getPreset = async (name) => (await GET(`/preset?name=${enc(name)}`)).json

beforeAll(async () => {
  t = await startTarget()
  const status = await GET('/status')
  expect(status.status, 'target must serve /status to run the suite').toBe(200)
  snapshot = status.json

  const created = await POST(`/preset?action=create&name=${enc(P)}`)
  expect(created.status).toBe(201)
})

afterAll(async () => {
  if (!t) return
  try {
    // Restore globals from the snapshot
    if (snapshot) {
      await PUT(`/volume?value=${snapshot.volume}`)
      await PUT(`/mute?state=${snapshot.mute.muted ? 'on' : 'off'}`)
      await PUT(`/mute/percent?percent=${snapshot.mute.percent}`)
      for (const s of ['left', 'right', 'sub']) {
        await PUT(`/gains/speaker?speaker=${s}&value=${snapshot.speakerGains[s]}`)
      }
      await PUT('/gains/input', snapshot.inputGains)
      if (snapshot.tone.frequency >= 20 && snapshot.tone.volume > 0) {
        await PUT(`/generate/tone?frequency=${snapshot.tone.frequency}&volume=${snapshot.tone.volume}`)
      } else {
        await PUT('/generate/tone/stop')
      }
      await PUT(`/noise?level=${snapshot.noise.volume}`)
      if (snapshot.currentPreset) {
        await PUT(`/preset/active?name=${enc(snapshot.currentPreset)}`)
      }
    }
    // Delete every preset this run created
    const presets = (await GET('/presets')).json
    if (Array.isArray(presets)) {
      for (const p of presets) {
        if (p.name.startsWith(PREFIX)) await DEL(`/preset?name=${enc(p.name)}`)
      }
    }
  } finally {
    await t.stop()
  }
})

// ===== /status =====

describe('GET /status', () => {
  it('returns the full system shape with correct units', async () => {
    const res = await GET('/status')
    expect(res.status).toBe(200)
    const s = res.json

    // Speaker gains: 0-100 percent (the ESP stores 0-1 and reports x100)
    for (const ch of ['left', 'right', 'sub']) {
      expect(typeof s.speakerGains[ch]).toBe('number')
      expect(s.speakerGains[ch]).toBeGreaterThanOrEqual(0)
      expect(s.speakerGains[ch]).toBeLessThanOrEqual(100)
    }

    // Input gains: linear 0.0-1.0, analog input must be present
    for (const input of ['spdif', 'bluetooth', 'usb', 'tone', 'analog']) {
      expect(typeof s.inputGains[input], `inputGains.${input}`).toBe('number')
      expect(s.inputGains[input]).toBeGreaterThanOrEqual(0)
      expect(s.inputGains[input]).toBeLessThanOrEqual(1)
    }

    // Mute is nested: muted boolean + percent 0-100
    expect(typeof s.mute.muted).toBe('boolean')
    expect(typeof s.mute.percent).toBe('number')
    expect(s.mute.percent).toBeGreaterThanOrEqual(0)
    expect(s.mute.percent).toBeLessThanOrEqual(100)

    expect(typeof s.tone.frequency).toBe('number')
    expect(typeof s.tone.volume).toBe('number')
    expect(typeof s.noise.volume).toBe('number')

    expect(typeof s.currentPreset).toBe('string')
    expect(typeof s.volume).toBe('number')
    expect(s.volume).toBeGreaterThanOrEqual(0)
    expect(s.volume).toBeLessThanOrEqual(100)
  })
})

// ===== /volume =====

describe('PUT /volume', () => {
  it('sets the volume and echoes it back', async () => {
    const res = await PUT('/volume?value=43')
    expect(res.status).toBe(200)
    expect(res.json).toMatchObject({ success: true, volume: 43 })

    const status = (await GET('/status')).json
    expect(status.volume).toBe(43)
  })

  it('rejects out-of-range values with 400', async () => {
    expect((await PUT('/volume?value=101')).status).toBe(400)
    expect((await PUT('/volume?value=-1')).status).toBe(400)
  })
})

// ===== /mute and /mute/percent =====

describe('PUT /mute', () => {
  it('mutes and unmutes, replying with the muteChanged shape', async () => {
    const on = await PUT('/mute?state=on')
    expect(on.status).toBe(200)
    expect(on.json).toEqual({ messageType: 'muteChanged', muted: true })
    expect((await GET('/status')).json.mute.muted).toBe(true)

    const off = await PUT('/mute?state=off')
    expect(off.status).toBe(200)
    expect(off.json).toEqual({ messageType: 'muteChanged', muted: false })
    expect((await GET('/status')).json.mute.muted).toBe(false)
  })

  it('rejects states other than on/off with 400', async () => {
    expect((await PUT('/mute?state=banana')).status).toBe(400)
  })
})

describe('PUT /mute/percent', () => {
  it('sets the percent and replies with the mutePercentChanged shape', async () => {
    const res = await PUT('/mute/percent?percent=60')
    expect(res.status).toBe(200)
    expect(res.json).toEqual({ messageType: 'mutePercentChanged', mutePercent: 60 })
    expect((await GET('/status')).json.mute.percent).toBe(60)
  })

  it('accepts the 0 and 100 boundaries', async () => {
    expect((await PUT('/mute/percent?percent=0')).status).toBe(200)
    expect((await PUT('/mute/percent?percent=100')).status).toBe(200)
  })

  it('rejects values outside 0-100 with 400', async () => {
    expect((await PUT('/mute/percent?percent=101')).status).toBe(400)
    expect((await PUT('/mute/percent?percent=-5')).status).toBe(400)
  })
})

// ===== Gains =====

describe('PUT /gains/speaker', () => {
  it('sets a speaker gain on the 0-100 scale, visible in /status', async () => {
    const res = await PUT('/gains/speaker?speaker=left&value=55')
    expect(res.status).toBe(200)
    expect(res.json).toMatchObject({ success: true })
    // float32 round-trip on the device: compare approximately
    expect((await GET('/status')).json.speakerGains.left).toBeCloseTo(55, 3)
  })

  it('clamps out-of-range values into 0-100 like the ESP', async () => {
    expect((await PUT('/gains/speaker?speaker=right&value=150')).status).toBe(200)
    expect((await GET('/status')).json.speakerGains.right).toBeCloseTo(100, 3)
    expect((await PUT('/gains/speaker?speaker=right&value=-10')).status).toBe(200)
    expect((await GET('/status')).json.speakerGains.right).toBeCloseTo(0, 3)
  })

  it('rejects unknown speakers with 400', async () => {
    expect((await PUT('/gains/speaker?speaker=center&value=50')).status).toBe(400)
  })
})

describe('PUT /gains/input', () => {
  it('sets all input gains from a JSON body (0.0-1.0 linear)', async () => {
    const gains = { bluetooth: 0.75, spdif: 0.25, usb: 0.5, tone: 0.125, analog: 0.375 }
    const res = await PUT('/gains/input', gains)
    expect(res.status).toBe(200)
    expect(res.json).toMatchObject({ success: true })

    const status = (await GET('/status')).json
    for (const [k, v] of Object.entries(gains)) {
      expect(status.inputGains[k], `inputGains.${k}`).toBeCloseTo(v, 4)
    }
  })

  it('leaves omitted keys unchanged on a partial body', async () => {
    await PUT('/gains/input', { bluetooth: 0.75, spdif: 0.25, usb: 0.5, tone: 0.125, analog: 0.375 })
    const res = await PUT('/gains/input', { bluetooth: 0.5 })
    expect(res.status).toBe(200)

    const status = (await GET('/status')).json
    expect(status.inputGains.bluetooth).toBeCloseTo(0.5, 4)
    expect(status.inputGains.spdif).toBeCloseTo(0.25, 4)
    expect(status.inputGains.usb).toBeCloseTo(0.5, 4)
    expect(status.inputGains.tone).toBeCloseTo(0.125, 4)
    expect(status.inputGains.analog).toBeCloseTo(0.375, 4)
  })
})

// ===== Signal generator =====

describe('signal generator', () => {
  it('PUT /generate/tone sets frequency and volume', async () => {
    const res = await PUT('/generate/tone?frequency=440&volume=1')
    expect(res.status).toBe(200)
    expect(res.json).toEqual({ toneFrequency: 440, toneVolume: 1 })

    const status = (await GET('/status')).json
    expect(status.tone.frequency).toBe(440)
    expect(status.tone.volume).toBe(1)
  })

  it('PUT /generate/tone/stop zeroes both frequency and volume', async () => {
    await PUT('/generate/tone?frequency=440&volume=1')
    const res = await PUT('/generate/tone/stop')
    expect(res.status).toBe(200)
    expect(res.json).toEqual({ toneFrequency: 0, toneVolume: 0 })

    const status = (await GET('/status')).json
    expect(status.tone.frequency).toBe(0)
    expect(status.tone.volume).toBe(0)
  })

  it('PUT /generate/tone rejects out-of-range values with 400', async () => {
    expect((await PUT('/generate/tone?frequency=19&volume=1')).status).toBe(400)
    expect((await PUT('/generate/tone?frequency=20001&volume=1')).status).toBe(400)
    expect((await PUT('/generate/tone?frequency=440&volume=101')).status).toBe(400)
  })

  it('PUT /noise sets and clears the noise level', async () => {
    const on = await PUT('/noise?level=1')
    expect(on.status).toBe(200)
    expect(on.json).toEqual({ noiseVolume: 1 })
    expect((await GET('/status')).json.noise.volume).toBe(1)

    const off = await PUT('/noise?level=0')
    expect(off.status).toBe(200)
    expect(off.json).toEqual({ noiseVolume: 0 })
  })

  it('PUT /noise rejects out-of-range levels with 400', async () => {
    expect((await PUT('/noise?level=101')).status).toBe(400)
    expect((await PUT('/noise?level=-1')).status).toBe(400)
  })
})

// ===== Preset CRUD =====

describe('preset CRUD', () => {
  it('GET /presets lists {name, isCurrent} with exactly one current', async () => {
    const res = await GET('/presets')
    expect(res.status).toBe(200)
    expect(Array.isArray(res.json)).toBe(true)
    for (const p of res.json) {
      expect(typeof p.name).toBe('string')
      expect(typeof p.isCurrent).toBe('boolean')
    }
    expect(res.json.filter((p) => p.isCurrent)).toHaveLength(1)
    expect(res.json.some((p) => p.name === P)).toBe(true)
  })

  it('a created preset starts with the documented defaults', async () => {
    const preset = await getPreset(P)
    expect(preset.name).toBe(P)
    expect(preset.isCurrent).toBe(false)
    expect(preset.speakerDelays).toMatchObject({ left: 0, right: 0, sub: 0 })
    expect(preset.isSpeakerDelayEnabled).toBe(false)
    expect(preset.crossoverFreq).toBe(80)
    expect(preset.isCrossoverEnabled).toBe(false)
    expect(preset.isFIREnabled).toBe(false)
    expect(preset.firLeft).toBe('')
    expect(preset.firRight).toBe('')
    expect(preset.firSub).toBe('')
    expect(preset.isPreferenceEQEnabled).toBe(false)
    // Default spl=0 preference set: three flat points at 100/1000/10000 Hz
    const spl0 = preset.preferenceEQ.find((set) => set.spl === 0)
    expect(spl0).toBeDefined()
    expect(spl0.peqs.map((p) => p.freq)).toEqual([100, 1000, 10000])
    for (const peq of spl0.peqs) {
      expect(peq.gain).toBe(0)
      expect(peq.q).toBe(1)
    }
  })

  it('POST create rejects a duplicate name with 409', async () => {
    expect((await POST(`/preset?action=create&name=${enc(P)}`)).status).toBe(409)
  })

  it('POST create without a name is a 400', async () => {
    expect((await POST('/preset?action=create')).status).toBe(400)
  })

  it('copy duplicates settings under source/destination params', async () => {
    const copyName = `${PREFIX}-copy`
    await PUT(`/preset/crossover?preset_name=${enc(P)}&frequency=120`)

    const res = await POST(`/preset?action=copy&source=${enc(P)}&destination=${enc(copyName)}`)
    expect(res.status).toBe(201)

    const copy = await getPreset(copyName)
    expect(copy.name).toBe(copyName)
    expect(copy.isCurrent).toBe(false)
    expect(copy.crossoverFreq).toBe(120)
    // EQ sets travel with the copy
    const spl0 = copy.preferenceEQ.find((set) => set.spl === 0)
    expect(spl0.peqs.length).toBeGreaterThan(0)
  })

  it('copy to an existing destination is a 409, from a missing source a 404', async () => {
    expect((await POST(`/preset?action=copy&source=${enc(P)}&destination=${enc(P)}`)).status).toBe(409)
    expect((await POST(`/preset?action=copy&source=${enc(PREFIX + '-nope')}&destination=${enc(PREFIX + '-dest2')}`)).status).toBe(404)
  })

  it('rename uses old_name/new_name and carries the EQ data along', async () => {
    const oldName = `${PREFIX}-copy`
    const newName = `${PREFIX}-renamed`

    const res = await PUT(`/preset?action=rename&old_name=${enc(oldName)}&new_name=${enc(newName)}`)
    expect(res.status).toBe(200)

    expect((await GET(`/preset?name=${enc(oldName)}`)).status).toBe(404)
    const renamed = await getPreset(newName)
    expect(renamed.crossoverFreq).toBe(120)
    expect(renamed.preferenceEQ.find((set) => set.spl === 0).peqs.length).toBeGreaterThan(0)
  })

  it('rename conflicts are 409 and missing presets 404', async () => {
    expect((await PUT(`/preset?action=rename&old_name=${enc(PREFIX + '-renamed')}&new_name=${enc(P)}`)).status).toBe(409)
    expect((await PUT(`/preset?action=rename&old_name=${enc(PREFIX + '-nope')}&new_name=${enc(PREFIX + '-whatever')}`)).status).toBe(404)
  })

  it('DELETE removes a preset, and a second delete is a 404', async () => {
    const name = `${PREFIX}-renamed`
    const res = await DEL(`/preset?name=${enc(name)}`)
    expect(res.status).toBe(200)
    expect((await GET(`/preset?name=${enc(name)}`)).status).toBe(404)
    expect((await DEL(`/preset?name=${enc(name)}`)).status).toBe(404)
  })

  it('deleting the active preset falls back to another remaining preset', async () => {
    const name = `${PREFIX}-active-victim`
    await POST(`/preset?action=create&name=${enc(name)}`)
    await PUT(`/preset/active?name=${enc(name)}`)

    expect((await DEL(`/preset?name=${enc(name)}`)).status).toBe(200)

    const presets = (await GET('/presets')).json
    const current = presets.filter((p) => p.isCurrent)
    expect(current).toHaveLength(1)
    expect(current[0].name).not.toBe(name)

    // Put the original active preset back
    await PUT(`/preset/active?name=${enc(snapshot.currentPreset)}`)
  })

  it('GET /preset for a missing preset is a 404', async () => {
    expect((await GET(`/preset?name=${enc(PREFIX + '-missing')}`)).status).toBe(404)
  })
})

// ===== Active preset =====

describe('PUT /preset/active', () => {
  it('activates the named preset', async () => {
    const res = await PUT(`/preset/active?name=${enc(P)}`)
    expect(res.status).toBe(200)

    const presets = (await GET('/presets')).json
    expect(presets.find((p) => p.name === P).isCurrent).toBe(true)
    expect((await GET('/status')).json.currentPreset).toBe(P)

    // Restore
    await PUT(`/preset/active?name=${enc(snapshot.currentPreset)}`)
    expect((await GET('/status')).json.currentPreset).toBe(snapshot.currentPreset)
  })

  it('missing name is 400, unknown preset 404', async () => {
    expect((await PUT('/preset/active')).status).toBe(400)
    expect((await PUT(`/preset/active?name=${enc(PREFIX + '-missing')}`)).status).toBe(404)
  })
})

// ===== Preset gains =====

describe('/preset/gains', () => {
  it('GET returns per-speaker gains on the 0-100 scale', async () => {
    const res = await GET(`/preset/gains?preset_name=${enc(P)}`)
    expect(res.status).toBe(200)
    for (const ch of ['left', 'right', 'sub']) {
      expect(typeof res.json[ch]).toBe('number')
      expect(res.json[ch]).toBeGreaterThanOrEqual(0)
      expect(res.json[ch]).toBeLessThanOrEqual(100)
    }
  })

  it('PUT sets gains from a JSON body', async () => {
    const res = await PUT(`/preset/gains?preset_name=${enc(P)}`, { left: 80, right: 70, sub: 60 })
    expect(res.status).toBe(200)
    expect(res.json).toMatchObject({ success: true })

    const gains = (await GET(`/preset/gains?preset_name=${enc(P)}`)).json
    expect(gains.left).toBeCloseTo(80, 3)
    expect(gains.right).toBeCloseTo(70, 3)
    expect(gains.sub).toBeCloseTo(60, 3)
  })

  it('a partial body does not zero the omitted channels', async () => {
    await PUT(`/preset/gains?preset_name=${enc(P)}`, { left: 80, right: 70, sub: 60 })
    const res = await PUT(`/preset/gains?preset_name=${enc(P)}`, { sub: 40 })
    expect(res.status).toBe(200)

    const gains = (await GET(`/preset/gains?preset_name=${enc(P)}`)).json
    expect(gains.left).toBeCloseTo(80, 3)
    expect(gains.right).toBeCloseTo(70, 3)
    expect(gains.sub).toBeCloseTo(40, 3)
  })

  it('missing preset_name is 400 and an unknown preset 404', async () => {
    expect((await GET('/preset/gains')).status).toBe(400)
    expect((await GET(`/preset/gains?preset_name=${enc(PREFIX + '-missing')}`)).status).toBe(404)
    expect((await PUT(`/preset/gains?preset_name=${enc(PREFIX + '-missing')}`, { left: 50 })).status).toBe(404)
  })
})

// ===== EQ =====

describe('/preset/eq', () => {
  const LEAK_CHECK = `${PREFIX}-eq-leak-check`

  it('saves an array of points (204), readable via GET /preset', async () => {
    await POST(`/preset?action=create&name=${enc(LEAK_CHECK)}`)

    const points = [
      { freq: 250, gain: -6, q: 0.5 },
      { freq: 1000, gain: 3.5, q: 2 },
    ]
    const res = await PUT(`/preset/eq?preset_name=${enc(P)}`, points)
    expect(res.status).toBe(204)
    expect(res.text).toBe('')

    const spl0 = (await getPreset(P)).preferenceEQ.find((set) => set.spl === 0)
    expect(spl0.peqs).toHaveLength(2)
    expect(spl0.peqs[0].freq).toBeCloseTo(250, 3)
    expect(spl0.peqs[0].gain).toBeCloseTo(-6, 3)
    expect(spl0.peqs[0].q).toBeCloseTo(0.5, 3)
    expect(spl0.peqs[1].freq).toBeCloseTo(1000, 3)
    expect(spl0.peqs[1].gain).toBeCloseTo(3.5, 3)
    expect(spl0.peqs[1].q).toBeCloseTo(2, 3)
  })

  it('honors preset_name: the save does not leak into other presets', async () => {
    // LEAK_CHECK was created before the save above; it must still hold the
    // untouched default 3 flat points
    const spl0 = (await getPreset(LEAK_CHECK)).preferenceEQ.find((set) => set.spl === 0)
    expect(spl0.peqs.map((p) => p.freq)).toEqual([100, 1000, 10000])
    for (const peq of spl0.peqs) expect(peq.gain).toBe(0)
  })

  it('clamps out-of-range values instead of rejecting them', async () => {
    const res = await PUT(`/preset/eq?preset_name=${enc(P)}`, [{ freq: 5, gain: 40, q: 50 }])
    expect(res.status).toBe(204)

    const spl0 = (await getPreset(P)).preferenceEQ.find((set) => set.spl === 0)
    expect(spl0.peqs[0].freq).toBeCloseTo(20, 3)
    expect(spl0.peqs[0].gain).toBeCloseTo(15, 3)
    expect(spl0.peqs[0].q).toBeCloseTo(10, 3)
  })

  it('rejects more than 15 points and non-array bodies with 400', async () => {
    const tooMany = Array.from({ length: 16 }, () => ({ freq: 1000, gain: 0, q: 1 }))
    expect((await PUT(`/preset/eq?preset_name=${enc(P)}`, tooMany)).status).toBe(400)
    expect((await PUT(`/preset/eq?preset_name=${enc(P)}`, { freq: 1000 })).status).toBe(400)
  })

  it('404s for an unknown preset', async () => {
    expect((await PUT(`/preset/eq?preset_name=${enc(PREFIX + '-missing')}`, [])).status).toBe(404)
  })
})

describe('/preset/eq/point', () => {
  beforeAll(async () => {
    // Known starting state: two points
    await PUT(`/preset/eq?preset_name=${enc(P)}`, [
      { freq: 250, gain: -6, q: 0.5 },
      { freq: 1000, gain: 3.5, q: 2 },
    ])
  })

  it('updates a point in place (204)', async () => {
    const res = await PUT(`/preset/eq/point?preset_name=${enc(P)}`, { id: 1, freq: 2000, gain: -2.5, q: 4 })
    expect(res.status).toBe(204)

    const spl0 = (await getPreset(P)).preferenceEQ.find((set) => set.spl === 0)
    expect(spl0.peqs).toHaveLength(2)
    expect(spl0.peqs[1].freq).toBeCloseTo(2000, 3)
    expect(spl0.peqs[1].gain).toBeCloseTo(-2.5, 3)
    expect(spl0.peqs[1].q).toBeCloseTo(4, 3)
    // Point 0 untouched
    expect(spl0.peqs[0].freq).toBeCloseTo(250, 3)
  })

  it('appends directly after the last point (204)', async () => {
    const res = await PUT(`/preset/eq/point?preset_name=${enc(P)}`, { id: 2, freq: 8000, gain: 1.5, q: 1 })
    expect(res.status).toBe(204)

    const spl0 = (await getPreset(P)).preferenceEQ.find((set) => set.spl === 0)
    expect(spl0.peqs).toHaveLength(3)
    expect(spl0.peqs[2].freq).toBeCloseTo(8000, 3)
  })

  it('rejects an id that would leave a gap with 400', async () => {
    // 3 points exist; id=3 would append, id=4 leaves a gap
    expect((await PUT(`/preset/eq/point?preset_name=${enc(P)}`, { id: 4, freq: 100, gain: 1, q: 1 })).status).toBe(400)
  })

  it('rejects ids outside 0-14 with 400', async () => {
    expect((await PUT(`/preset/eq/point?preset_name=${enc(P)}`, { id: 15, freq: 100, gain: 1, q: 1 })).status).toBe(400)
    expect((await PUT(`/preset/eq/point?preset_name=${enc(P)}`, { id: -1, freq: 100, gain: 1, q: 1 })).status).toBe(400)
    expect((await PUT(`/preset/eq/point?preset_name=${enc(P)}`, { freq: 100, gain: 1, q: 1 })).status).toBe(400)
  })

  it('404s for an unknown preset', async () => {
    expect((await PUT(`/preset/eq/point?preset_name=${enc(PREFIX + '-missing')}`, { id: 0 })).status).toBe(404)
  })
})

describe('PUT /preset/eq/enabled', () => {
  it('toggles the preference EQ, replying with the eqEnabledChanged shape', async () => {
    const on = await PUT(`/preset/eq/enabled?preset_name=${enc(P)}&type=pref&enabled=on`)
    expect(on.status).toBe(200)
    expect(on.json).toEqual({ messageType: 'eqEnabledChanged', presetName: P, status: 'ok', enabled: true })
    expect((await getPreset(P)).isPreferenceEQEnabled).toBe(true)

    const off = await PUT(`/preset/eq/enabled?preset_name=${enc(P)}&type=pref&enabled=off`)
    expect(off.json).toEqual({ messageType: 'eqEnabledChanged', presetName: P, status: 'ok', enabled: false })
    expect((await getPreset(P)).isPreferenceEQEnabled).toBe(false)
  })

  it('rejects invalid enabled values with 400', async () => {
    expect((await PUT(`/preset/eq/enabled?preset_name=${enc(P)}&type=pref&enabled=maybe`)).status).toBe(400)
  })
})

// ===== Crossover =====

describe('/preset/crossover', () => {
  it('sets the frequency, replying with the crossoverChanged shape', async () => {
    const res = await PUT(`/preset/crossover?preset_name=${enc(P)}&frequency=120`)
    expect(res.status).toBe(200)
    expect(res.json).toEqual({ messageType: 'crossoverChanged', presetName: P, status: 'ok', crossoverFreq: 120 })
    expect((await getPreset(P)).crossoverFreq).toBe(120)
  })

  it('accepts the 20 and 20000 Hz boundaries', async () => {
    expect((await PUT(`/preset/crossover?preset_name=${enc(P)}&frequency=20`)).status).toBe(200)
    expect((await PUT(`/preset/crossover?preset_name=${enc(P)}&frequency=20000`)).status).toBe(200)
  })

  it('rejects frequencies outside 20-20000 with 400', async () => {
    expect((await PUT(`/preset/crossover?preset_name=${enc(P)}&frequency=19`)).status).toBe(400)
    expect((await PUT(`/preset/crossover?preset_name=${enc(P)}&frequency=20001`)).status).toBe(400)
  })

  it('404s for an unknown preset', async () => {
    expect((await PUT(`/preset/crossover?preset_name=${enc(PREFIX + '-missing')}&frequency=100`)).status).toBe(404)
  })

  it('toggles enablement with the crossoverEnabledChanged shape', async () => {
    const on = await PUT(`/preset/crossover/enabled?preset_name=${enc(P)}&enabled=on`)
    expect(on.status).toBe(200)
    expect(on.json).toEqual({ messageType: 'crossoverEnabledChanged', presetName: P, status: 'ok', crossoverEnabled: true })
    expect((await getPreset(P)).isCrossoverEnabled).toBe(true)

    const off = await PUT(`/preset/crossover/enabled?preset_name=${enc(P)}&enabled=off`)
    expect(off.json).toEqual({ messageType: 'crossoverEnabledChanged', presetName: P, status: 'ok', crossoverEnabled: false })
    expect((await getPreset(P)).isCrossoverEnabled).toBe(false)
  })
})

// ===== Speaker delays =====

describe('/preset/delay', () => {
  it('sets a per-speaker delay in microseconds, replying with the delayChanged shape', async () => {
    const res = await PUT(`/preset/delay?preset_name=${enc(P)}&speaker=left&value=1234.5`)
    expect(res.status).toBe(200)
    expect(res.json).toEqual({ messageType: 'delayChanged', presetName: P, status: 'ok', speaker: 'left', delayUs: 1234.5 })
    expect((await getPreset(P)).speakerDelays.left).toBeCloseTo(1234.5, 3)
  })

  it('keeps each speaker independent', async () => {
    await PUT(`/preset/delay?preset_name=${enc(P)}&speaker=right&value=500`)
    await PUT(`/preset/delay?preset_name=${enc(P)}&speaker=sub&value=20000`)

    const delays = (await getPreset(P)).speakerDelays
    expect(delays.left).toBeCloseTo(1234.5, 3)
    expect(delays.right).toBeCloseTo(500, 3)
    expect(delays.sub).toBeCloseTo(20000, 3)
  })

  it('rejects delays outside 0-20000us and unknown speakers with 400', async () => {
    expect((await PUT(`/preset/delay?preset_name=${enc(P)}&speaker=left&value=20001`)).status).toBe(400)
    expect((await PUT(`/preset/delay?preset_name=${enc(P)}&speaker=left&value=-1`)).status).toBe(400)
    expect((await PUT(`/preset/delay?preset_name=${enc(P)}&speaker=center&value=100`)).status).toBe(400)
  })

  it('404s for an unknown preset', async () => {
    expect((await PUT(`/preset/delay?preset_name=${enc(PREFIX + '-missing')}&speaker=left&value=100`)).status).toBe(404)
  })

  it('toggles enablement with the delayEnabledChanged shape', async () => {
    const on = await PUT(`/preset/delay/enabled?preset_name=${enc(P)}&enabled=on`)
    expect(on.status).toBe(200)
    expect(on.json).toEqual({ messageType: 'delayEnabledChanged', presetName: P, status: 'ok', enabled: true })
    expect((await getPreset(P)).isSpeakerDelayEnabled).toBe(true)

    const off = await PUT(`/preset/delay/enabled?preset_name=${enc(P)}&enabled=off`)
    expect(off.json).toEqual({ messageType: 'delayEnabledChanged', presetName: P, status: 'ok', enabled: false })
    expect((await getPreset(P)).isSpeakerDelayEnabled).toBe(false)
  })

  it('rejects invalid enabled values with 400', async () => {
    expect((await PUT(`/preset/delay/enabled?preset_name=${enc(P)}&enabled=maybe`)).status).toBe(400)
  })
})

// ===== FIR =====

describe('FIR filters', () => {
  it('GET /fir/files returns an array of filename strings', async () => {
    const res = await GET('/fir/files')
    expect(res.status).toBe(200)
    expect(Array.isArray(res.json)).toBe(true)
    for (const f of res.json) expect(typeof f).toBe('string')
  })

  it('PUT /preset/fir assigns a file per speaker, replying with the firChanged shape', async () => {
    const res = await PUT(`/preset/fir?preset_name=${enc(P)}&speaker=left&file=${enc('contract-test-fir.txt')}`)
    expect(res.status).toBe(200)
    expect(res.json).toEqual({
      messageType: 'firChanged',
      presetName: P,
      status: 'ok',
      speaker: 'left',
      filename: 'contract-test-fir.txt',
    })
    expect((await getPreset(P)).firLeft).toBe('contract-test-fir.txt')
  })

  it('an empty file clears the filter', async () => {
    const res = await PUT(`/preset/fir?preset_name=${enc(P)}&speaker=left&file=`)
    expect(res.status).toBe(200)
    expect(res.json).toMatchObject({ messageType: 'firChanged', speaker: 'left', filename: '' })
    expect((await getPreset(P)).firLeft).toBe('')
  })

  it('rejects unknown speakers with 400 and unknown presets with 404', async () => {
    expect((await PUT(`/preset/fir?preset_name=${enc(P)}&speaker=center&file=x.txt`)).status).toBe(400)
    expect((await PUT(`/preset/fir?preset_name=${enc(PREFIX + '-missing')}&speaker=left&file=x.txt`)).status).toBe(404)
  })

  it('toggles enablement with the firEnabledChanged shape', async () => {
    const on = await PUT(`/preset/fir/enabled?preset_name=${enc(P)}&state=on`)
    expect(on.status).toBe(200)
    expect(on.json).toEqual({ messageType: 'firEnabledChanged', presetName: P, status: 'ok', FIRFiltersEnabled: true })
    expect((await getPreset(P)).isFIREnabled).toBe(true)

    const off = await PUT(`/preset/fir/enabled?preset_name=${enc(P)}&state=off`)
    expect(off.json).toEqual({ messageType: 'firEnabledChanged', presetName: P, status: 'ok', FIRFiltersEnabled: false })
    expect((await getPreset(P)).isFIREnabled).toBe(false)
  })

  it('rejects invalid states with 400', async () => {
    expect((await PUT(`/preset/fir/enabled?preset_name=${enc(P)}&state=maybe`)).status).toBe(400)
  })
})

// ===== Backup =====

describe('GET /backup', () => {
  it('responds 200 with a body', async () => {
    const res = await GET('/backup')
    expect(res.status).toBe(200)
    expect(res.text.length).toBeGreaterThan(0)
  })
})

// ===== Websocket broadcasts =====

describe('websocket broadcasts', () => {
  let ws

  beforeAll(async () => {
    ws = await connectWs(t.wsUrl)
  })

  afterAll(async () => {
    if (ws) ws.close()
    // leave globals as the outer afterAll expects to restore them anyway
  })

  it('volumeChanged', async () => {
    const wait = ws.expect((m) => m.messageType === 'volumeChanged')
    await PUT('/volume?value=47')
    expect(await wait).toEqual({ messageType: 'volumeChanged', volume: 47 })
  })

  it('muteChanged', async () => {
    const wait = ws.expect((m) => m.messageType === 'muteChanged')
    await PUT('/mute?state=on')
    expect(await wait).toEqual({ messageType: 'muteChanged', muted: true })

    const wait2 = ws.expect((m) => m.messageType === 'muteChanged')
    await PUT('/mute?state=off')
    expect(await wait2).toEqual({ messageType: 'muteChanged', muted: false })
  })

  it('mutePercentChanged', async () => {
    const wait = ws.expect((m) => m.messageType === 'mutePercentChanged')
    await PUT('/mute/percent?percent=42')
    expect(await wait).toEqual({ messageType: 'mutePercentChanged', mutePercent: 42 })
  })

  it('activePresetChanged carries activePresetName and activePresetIndex', async () => {
    const wait = ws.expect((m) => m.messageType === 'activePresetChanged' && m.activePresetName === P)
    await PUT(`/preset/active?name=${enc(P)}`)
    const msg = await wait
    expect(msg.activePresetName).toBe(P)
    expect(typeof msg.activePresetIndex).toBe('number')

    // Restore the original active preset (also a broadcast; consume it)
    const waitRestore = ws.expect((m) => m.messageType === 'activePresetChanged')
    await PUT(`/preset/active?name=${enc(snapshot.currentPreset)}`)
    await waitRestore
  })

  it('crossoverChanged', async () => {
    const wait = ws.expect((m) => m.messageType === 'crossoverChanged' && m.presetName === P)
    await PUT(`/preset/crossover?preset_name=${enc(P)}&frequency=95`)
    expect(await wait).toEqual({ messageType: 'crossoverChanged', presetName: P, status: 'ok', crossoverFreq: 95 })
  })

  it('crossoverEnabledChanged', async () => {
    const wait = ws.expect((m) => m.messageType === 'crossoverEnabledChanged' && m.presetName === P)
    await PUT(`/preset/crossover/enabled?preset_name=${enc(P)}&enabled=off`)
    expect(await wait).toEqual({ messageType: 'crossoverEnabledChanged', presetName: P, status: 'ok', crossoverEnabled: false })
  })

  it('delayChanged', async () => {
    const wait = ws.expect((m) => m.messageType === 'delayChanged' && m.presetName === P)
    await PUT(`/preset/delay?preset_name=${enc(P)}&speaker=sub&value=750`)
    expect(await wait).toEqual({ messageType: 'delayChanged', presetName: P, status: 'ok', speaker: 'sub', delayUs: 750 })
  })

  it('delayEnabledChanged', async () => {
    const wait = ws.expect((m) => m.messageType === 'delayEnabledChanged' && m.presetName === P)
    await PUT(`/preset/delay/enabled?preset_name=${enc(P)}&enabled=off`)
    expect(await wait).toEqual({ messageType: 'delayEnabledChanged', presetName: P, status: 'ok', enabled: false })
  })

  it('eqPointsChanged reports the count of saved points', async () => {
    const wait = ws.expect((m) => m.messageType === 'eqPointsChanged' && m.presetName === P)
    await PUT(`/preset/eq?preset_name=${enc(P)}`, [
      { freq: 100, gain: 2, q: 1 },
      { freq: 4000, gain: -3, q: 2 },
    ])
    expect(await wait).toEqual({
      messageType: 'eqPointsChanged',
      presetName: P,
      status: 'ok',
      eqType: 'pref',
      spl: 0,
      numPoints: 2,
    })
  })

  it('eqEnabledChanged', async () => {
    const wait = ws.expect((m) => m.messageType === 'eqEnabledChanged' && m.presetName === P)
    await PUT(`/preset/eq/enabled?preset_name=${enc(P)}&type=pref&enabled=off`)
    expect(await wait).toEqual({ messageType: 'eqEnabledChanged', presetName: P, status: 'ok', enabled: false })
  })

  it('firChanged', async () => {
    const wait = ws.expect((m) => m.messageType === 'firChanged' && m.presetName === P)
    await PUT(`/preset/fir?preset_name=${enc(P)}&speaker=right&file=${enc('ws-test.txt')}`)
    expect(await wait).toEqual({
      messageType: 'firChanged',
      presetName: P,
      status: 'ok',
      speaker: 'right',
      filename: 'ws-test.txt',
    })
    // Clear it again (consume the second broadcast)
    const waitClear = ws.expect((m) => m.messageType === 'firChanged' && m.filename === '')
    await PUT(`/preset/fir?preset_name=${enc(P)}&speaker=right&file=`)
    await waitClear
  })

  it('firEnabledChanged', async () => {
    const wait = ws.expect((m) => m.messageType === 'firEnabledChanged' && m.presetName === P)
    await PUT(`/preset/fir/enabled?preset_name=${enc(P)}&state=off`)
    expect(await wait).toEqual({ messageType: 'firEnabledChanged', presetName: P, status: 'ok', FIRFiltersEnabled: false })
  })

  it('tone broadcasts carry toneFrequency/toneVolume without a messageType', async () => {
    const wait = ws.expect((m) => m.toneFrequency === 550 && m.toneVolume === 1)
    await PUT('/generate/tone?frequency=550&volume=1')
    expect(await wait).toEqual({ toneFrequency: 550, toneVolume: 1 })

    const waitStop = ws.expect((m) => m.toneFrequency === 0 && m.toneVolume === 0)
    await PUT('/generate/tone/stop')
    expect(await waitStop).toEqual({ toneFrequency: 0, toneVolume: 0 })
  })

  it('noise broadcasts carry noiseVolume without a messageType', async () => {
    const wait = ws.expect((m) => m.noiseVolume === 1)
    await PUT('/noise?level=1')
    expect(await wait).toEqual({ noiseVolume: 1 })

    const waitOff = ws.expect((m) => m.noiseVolume === 0)
    await PUT('/noise?level=0')
    await waitOff
  })
})

// ===== Destructive semantics (mock only) =====
// These delete every preset but one, so they never run against a device.

describe('delete-last-preset semantics (mock only)', () => {
  itMockOnly('refuses to delete the last remaining preset with 400', async () => {
    const presets = (await GET('/presets')).json
    // Delete down to a single preset
    for (const p of presets.slice(0, -1)) {
      expect((await DEL(`/preset?name=${enc(p.name)}`)).status).toBe(200)
    }
    const last = presets[presets.length - 1]
    expect((await DEL(`/preset?name=${enc(last.name)}`)).status).toBe(400)

    // The survivor must be the active preset
    const remaining = (await GET('/presets')).json
    expect(remaining).toHaveLength(1)
    expect(remaining[0].isCurrent).toBe(true)
  })

  itMockOnly('POST /restore acknowledges a body', async () => {
    const res = await POST('/restore', { anything: true })
    expect(res.status).toBe(200)
  })
})
