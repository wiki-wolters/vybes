/*
 * Vybes HTTP + websocket API contract suite.
 *
 * For system-level endpoints the contract's source of truth is the existing
 * ESP firmware (ESP/esp-web-server/api_*.cpp + web_server.cpp). For the V1
 * preset/output/crossover endpoints (docs/CHANNEL_ARCHITECTURE.md) the mock
 * server LEADS the contract: this suite is the spec the new ESP32-S3
 * firmware must implement.
 *
 * Modes:
 *  - `npm run test:contract`            hermetic: spawns mock-server/server.js
 *    on ephemeral ports with a throwaway sqlite DB (see harness.js).
 *  - `VYBES_API_URL=http://vybes.local npm run test:contract`   real device.
 *
 * Real-device politeness:
 *  - All preset mutations happen on uniquely named "contract-test-…" presets
 *    created by the suite and deleted in afterAll.
 *  - Global values the suite touches (mute, mute percent, speaker gains,
 *    input gains, tone, noise, active preset) are snapshotted from /status
 *    in beforeAll and restored in afterAll. Master volume is per-preset, so
 *    it is restored by name against the preset that owns it.
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
const V = `${PREFIX}-volume` // per-preset master volume tests (activated + levelled)

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
      // Master volume lives on a preset: name the one it was read from so
      // the restore doesn't depend on which preset is active right now
      const volumeTarget = snapshot.currentPreset
        ? `&preset_name=${enc(snapshot.currentPreset)}`
        : ''
      await PUT(`/volume?value=${snapshot.volume}${volumeTarget}`)
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
    expect(typeof s.deviceName).toBe('string')
    expect(typeof s.volume).toBe('number')
    expect(s.volume).toBeGreaterThanOrEqual(0)
    expect(s.volume).toBeLessThanOrEqual(100)
  })
})

// ===== /device/name =====

describe('PUT /device/name', () => {
  // Mock-only: renaming a real device restarts its mDNS announcement, so
  // "<name>.local" would stop resolving for the rest of the suite.
  itMockOnly('renames the device, replying with the deviceNameChanged shape', async () => {
    const original = (await GET('/status')).json.deviceName
    const res = await PUT('/device/name?name=contract-test-dev')
    expect(res.status).toBe(200)
    expect(res.json).toEqual({ messageType: 'deviceNameChanged', deviceName: 'contract-test-dev' })
    expect((await GET('/status')).json.deviceName).toBe('contract-test-dev')

    // Restore
    expect((await PUT(`/device/name?name=${enc(original)}`)).status).toBe(200)
  })

  it('rejects names that are not a valid DNS label with 400', async () => {
    expect((await PUT('/device/name?name=')).status).toBe(400)
    expect((await PUT('/device/name?name=Vybes')).status).toBe(400) // uppercase
    expect((await PUT('/device/name?name=-vybes')).status).toBe(400) // leading dash
    expect((await PUT('/device/name?name=vybes-')).status).toBe(400) // trailing dash
    expect((await PUT(`/device/name?name=${enc('my dsp')}`)).status).toBe(400) // space
    expect((await PUT(`/device/name?name=${enc('x'.repeat(25))}`)).status).toBe(400) // too long
  })
})

// ===== /volume =====

// Master volume is stored per preset: each one remembers the level it was
// last played at, and activating a preset restores it. Without preset_name
// the write lands on the active preset (the live master volume).
describe('PUT /volume', () => {
  it('sets the volume and echoes it back', async () => {
    const res = await PUT('/volume?value=43')
    expect(res.status).toBe(200)
    expect(res.json).toMatchObject({ success: true, volume: 43 })

    const status = (await GET('/status')).json
    expect(status.volume).toBe(43)
    expect(res.json.presetName).toBe(status.currentPreset)
  })

  it('rejects out-of-range values with 400', async () => {
    expect((await PUT('/volume?value=101')).status).toBe(400)
    expect((await PUT('/volume?value=-1')).status).toBe(400)
  })

  // A preset of its own: activating one and moving its level would otherwise
  // leave P dirty for the "created preset defaults" checks below
  it('preset_name sets that preset\'s stored level without touching the live one', async () => {
    await POST(`/preset?action=create&name=${enc(V)}`)
    expect((await getPreset(V)).volume).toBe(50) // documented default
    const before = (await GET('/status')).json.volume

    const res = await PUT(`/volume?value=17&preset_name=${enc(V)}`)
    expect(res.status).toBe(200)
    expect(res.json).toMatchObject({ success: true, presetName: V, volume: 17 })

    expect((await getPreset(V)).volume).toBe(17)
    // V is not the active preset, so nothing about the live level changed
    expect((await GET('/status')).json.volume).toBe(before)
  })

  it('unknown preset_name is a 404', async () => {
    expect((await PUT(`/volume?value=20&preset_name=${enc(PREFIX + '-missing')}`)).status).toBe(404)
  })

  it('activating a preset restores the volume it was last played at', async () => {
    await PUT(`/volume?value=31&preset_name=${enc(V)}`)

    await PUT(`/preset/active?name=${enc(V)}`)
    expect((await GET('/status')).json.volume).toBe(31)

    // Editing the live volume now writes V, not the preset we came from
    await PUT('/volume?value=64')
    expect((await getPreset(V)).volume).toBe(64)

    await PUT(`/preset/active?name=${enc(snapshot.currentPreset)}`)
    expect((await GET('/status')).json.volume).toBe(
      (await getPreset(snapshot.currentPreset)).volume
    )
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

  it('carries the SD playback level as the recorder key', async () => {
    const before = (await GET('/status')).json.inputGains
    expect(typeof before.recorder).toBe('number') // reported like the others

    const res = await PUT('/gains/input', { recorder: 0.5 })
    expect(res.status).toBe(200)
    const after = (await GET('/status')).json.inputGains
    expect(after.recorder).toBeCloseTo(0.5, 4)
    // Partial update: nothing else moved
    expect(after.bluetooth).toBeCloseTo(before.bluetooth, 4)

    await PUT('/gains/input', { recorder: before.recorder }) // restore
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

// ===== Auto delay alignment probe =====
// Real-device politeness: the chirp sequence only starts after a ~1.5s
// pre-roll, so starting and immediately stopping stays silent.

describe('delay alignment probe', () => {
  it('PUT /probe/delay/start returns the chirp schedule with a forward+reverse order', async () => {
    const res = await PUT('/probe/delay/start?level=40')
    try {
      expect(res.status).toBe(200)
      const s = res.json
      expect(s.status).toBe('ok')
      expect(s.sampleRate).toBe(44100)
      for (const field of ['preRollSamples', 'spacingSamples', 'chirpSamples', 'tailSamples', 'fadeSamples']) {
        expect(s[field], field).toBeGreaterThan(0)
      }
      expect(s.f0).toBeGreaterThan(0)
      expect(s.f1).toBeGreaterThan(s.f0)
      expect(s.level).toBe(40)
      // One chirp per enabled output, ascending, then the same list reversed
      expect(Array.isArray(s.order)).toBe(true)
      expect(s.order.length % 2).toBe(0)
      expect(s.order.length).toBeGreaterThanOrEqual(2)
      const n = s.order.length / 2
      const forward = s.order.slice(0, n)
      expect([...forward].sort((a, b) => a - b)).toEqual(forward)
      expect(s.order.slice(n)).toEqual([...forward].reverse())
    } finally {
      await PUT('/probe/delay/stop')
    }
  })

  it('PUT /probe/delay/start defaults the level and rejects bad ones with 400', async () => {
    expect((await PUT('/probe/delay/start?level=101')).status).toBe(400)
    expect((await PUT('/probe/delay/start?level=-1')).status).toBe(400)
    const res = await PUT('/probe/delay/start')
    try {
      expect(res.status).toBe(200)
      expect(res.json.level).toBe(50)
    } finally {
      await PUT('/probe/delay/stop')
    }
  })

  it('PUT /probe/delay/stop succeeds even when no probe is running', async () => {
    const res = await PUT('/probe/delay/stop')
    expect(res.status).toBe(200)
    expect(res.json).toEqual({ status: 'ok' })
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
    expect(preset.template).toBe('2.1')
    expect(preset.delaysEnabled).toBe(false)
    expect(preset.firEnabled).toBe(false)
    expect(preset.volume).toBe(50)
    expect(preset.inputEq.enabled).toBe(false)
    expect(preset.crossovers.find((x) => x.id === 'sub_xo').freq).toBe(80)
    for (const output of preset.outputs) {
      expect(output.delayUs).toBe(0)
      expect(output.gainDb).toBe(0)
      expect(output.fir).toBe('')
      expect(output.mute).toBe(false)
    }
    // Default spl=0 input EQ set: three flat points at 100/1000/10000 Hz
    const spl0 = preset.inputEq.sets.find((set) => set.spl === 0)
    expect(spl0).toBeDefined()
    expect(spl0.points.map((p) => p.freq)).toEqual([100, 1000, 10000])
    for (const point of spl0.points) {
      expect(point.gain).toBe(0)
      expect(point.q).toBe(1)
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
    await PUT(`/preset/crossover?preset_name=${enc(P)}&id=sub_xo&frequency=120`)

    const res = await POST(`/preset?action=copy&source=${enc(P)}&destination=${enc(copyName)}`)
    expect(res.status).toBe(201)

    const copy = await getPreset(copyName)
    expect(copy.name).toBe(copyName)
    expect(copy.isCurrent).toBe(false)
    expect(copy.crossovers.find((x) => x.id === 'sub_xo').freq).toBe(120)
    // EQ sets travel with the copy
    const spl0 = copy.inputEq.sets.find((set) => set.spl === 0)
    expect(spl0.points.length).toBeGreaterThan(0)
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
    expect(renamed.crossovers.find((x) => x.id === 'sub_xo').freq).toBe(120)
    expect(renamed.inputEq.sets.find((set) => set.spl === 0).points.length).toBeGreaterThan(0)
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

    const spl0 = (await getPreset(P)).inputEq.sets.find((set) => set.spl === 0)
    expect(spl0.points).toHaveLength(2)
    expect(spl0.points[0].freq).toBeCloseTo(250, 3)
    expect(spl0.points[0].gain).toBeCloseTo(-6, 3)
    expect(spl0.points[0].q).toBeCloseTo(0.5, 3)
    expect(spl0.points[1].freq).toBeCloseTo(1000, 3)
    expect(spl0.points[1].gain).toBeCloseTo(3.5, 3)
    expect(spl0.points[1].q).toBeCloseTo(2, 3)
  })

  it('honors preset_name: the save does not leak into other presets', async () => {
    // LEAK_CHECK was created before the save above; it must still hold the
    // untouched default 3 flat points
    const spl0 = (await getPreset(LEAK_CHECK)).inputEq.sets.find((set) => set.spl === 0)
    expect(spl0.points.map((p) => p.freq)).toEqual([100, 1000, 10000])
    for (const point of spl0.points) expect(point.gain).toBe(0)
  })

  it('clamps out-of-range values instead of rejecting them', async () => {
    const res = await PUT(`/preset/eq?preset_name=${enc(P)}`, [{ freq: 5, gain: 40, q: 50 }])
    expect(res.status).toBe(204)

    const spl0 = (await getPreset(P)).inputEq.sets.find((set) => set.spl === 0)
    expect(spl0.points[0].freq).toBeCloseTo(20, 3)
    expect(spl0.points[0].gain).toBeCloseTo(15, 3)
    expect(spl0.points[0].q).toBeCloseTo(10, 3)
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

    const spl0 = (await getPreset(P)).inputEq.sets.find((set) => set.spl === 0)
    expect(spl0.points).toHaveLength(2)
    expect(spl0.points[1].freq).toBeCloseTo(2000, 3)
    expect(spl0.points[1].gain).toBeCloseTo(-2.5, 3)
    expect(spl0.points[1].q).toBeCloseTo(4, 3)
    // Point 0 untouched
    expect(spl0.points[0].freq).toBeCloseTo(250, 3)
  })

  it('appends directly after the last point (204)', async () => {
    const res = await PUT(`/preset/eq/point?preset_name=${enc(P)}`, { id: 2, freq: 8000, gain: 1.5, q: 1 })
    expect(res.status).toBe(204)

    const spl0 = (await getPreset(P)).inputEq.sets.find((set) => set.spl === 0)
    expect(spl0.points).toHaveLength(3)
    expect(spl0.points[2].freq).toBeCloseTo(8000, 3)
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
    expect((await getPreset(P)).inputEq.enabled).toBe(true)

    const off = await PUT(`/preset/eq/enabled?preset_name=${enc(P)}&type=pref&enabled=off`)
    expect(off.json).toEqual({ messageType: 'eqEnabledChanged', presetName: P, status: 'ok', enabled: false })
    expect((await getPreset(P)).inputEq.enabled).toBe(false)
  })

  it('rejects invalid enabled values with 400', async () => {
    expect((await PUT(`/preset/eq/enabled?preset_name=${enc(P)}&type=pref&enabled=maybe`)).status).toBe(400)
  })
})

// ===== Crossover =====

describe('/preset/crossover', () => {
  it('sets the frequency, replying with the crossoverChanged shape', async () => {
    const res = await PUT(`/preset/crossover?preset_name=${enc(P)}&id=sub_xo&frequency=120`)
    expect(res.status).toBe(200)
    expect(res.json).toEqual({ messageType: 'crossoverChanged', presetName: P, status: 'ok', id: 'sub_xo', crossoverFreq: 120 })
    expect((await getPreset(P)).crossovers.find((x) => x.id === 'sub_xo').freq).toBe(120)
  })

  it('accepts the sub_xo 40 and 500 Hz range boundaries', async () => {
    expect((await PUT(`/preset/crossover?preset_name=${enc(P)}&id=sub_xo&frequency=40`)).status).toBe(200)
    expect((await PUT(`/preset/crossover?preset_name=${enc(P)}&id=sub_xo&frequency=500`)).status).toBe(200)
  })

  it('rejects frequencies outside the point range with 400', async () => {
    expect((await PUT(`/preset/crossover?preset_name=${enc(P)}&id=sub_xo&frequency=39`)).status).toBe(400)
    expect((await PUT(`/preset/crossover?preset_name=${enc(P)}&id=sub_xo&frequency=501`)).status).toBe(400)
  })

  it('requires the id parameter (400 when missing)', async () => {
    expect((await PUT(`/preset/crossover?preset_name=${enc(P)}&frequency=100`)).status).toBe(400)
    expect((await PUT(`/preset/crossover/enabled?preset_name=${enc(P)}&enabled=on`)).status).toBe(400)
  })

  it('404s for an unknown preset and an unknown crossover id', async () => {
    expect((await PUT(`/preset/crossover?preset_name=${enc(PREFIX + '-missing')}&id=sub_xo&frequency=100`)).status).toBe(404)
    expect((await PUT(`/preset/crossover?preset_name=${enc(P)}&id=nope_xo&frequency=100`)).status).toBe(404)
  })

  it('toggles enablement with the crossoverEnabledChanged shape', async () => {
    const on = await PUT(`/preset/crossover/enabled?preset_name=${enc(P)}&id=sub_xo&enabled=on`)
    expect(on.status).toBe(200)
    expect(on.json).toEqual({ messageType: 'crossoverEnabledChanged', presetName: P, status: 'ok', id: 'sub_xo', crossoverEnabled: true })
    expect((await getPreset(P)).outputs[0].hp.mode).toBe('xover')

    const off = await PUT(`/preset/crossover/enabled?preset_name=${enc(P)}&id=sub_xo&enabled=off`)
    expect(off.json).toEqual({ messageType: 'crossoverEnabledChanged', presetName: P, status: 'ok', id: 'sub_xo', crossoverEnabled: false })
    const preset = await getPreset(P)
    expect(preset.outputs[0].hp.mode).toBe('off')
    expect(preset.outputs[2].lp.mode).toBe('off')
  })
})

// ===== Speaker delays =====

describe('/preset/delay/enabled', () => {
  it('toggles enablement with the delayEnabledChanged shape', async () => {
    const on = await PUT(`/preset/delay/enabled?preset_name=${enc(P)}&enabled=on`)
    expect(on.status).toBe(200)
    expect(on.json).toEqual({ messageType: 'delayEnabledChanged', presetName: P, status: 'ok', enabled: true })
    expect((await getPreset(P)).delaysEnabled).toBe(true)

    const off = await PUT(`/preset/delay/enabled?preset_name=${enc(P)}&enabled=off`)
    expect(off.json).toEqual({ messageType: 'delayEnabledChanged', presetName: P, status: 'ok', enabled: false })
    expect((await getPreset(P)).delaysEnabled).toBe(false)
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

  it('PUT /preset/output/fir assigns and clears a file per output', async () => {
    const res = await PUT(`/preset/output/fir?preset_name=${enc(P)}&output=0&file=${enc('contract-test-fir.txt')}`)
    expect(res.status).toBe(200)
    expect(res.json).toMatchObject({ messageType: 'outputChanged', output: 0, changes: { fir: 'contract-test-fir.txt' } })
    expect(res.json.firPool.used).toBeGreaterThan(0)
    expect((await getPreset(P)).outputs[0].fir).toBe('contract-test-fir.txt')

    const clear = await PUT(`/preset/output/fir?preset_name=${enc(P)}&output=0&file=`)
    expect(clear.status).toBe(200)
    expect((await getPreset(P)).outputs[0].fir).toBe('')
  })

  it('toggles enablement with the firEnabledChanged shape', async () => {
    const on = await PUT(`/preset/fir/enabled?preset_name=${enc(P)}&state=on`)
    expect(on.status).toBe(200)
    expect(on.json).toEqual({ messageType: 'firEnabledChanged', presetName: P, status: 'ok', FIRFiltersEnabled: true })
    expect((await getPreset(P)).firEnabled).toBe(true)

    const off = await PUT(`/preset/fir/enabled?preset_name=${enc(P)}&state=off`)
    expect(off.json).toEqual({ messageType: 'firEnabledChanged', presetName: P, status: 'ok', FIRFiltersEnabled: false })
    expect((await getPreset(P)).firEnabled).toBe(false)
  })

  it('rejects invalid states with 400', async () => {
    expect((await PUT(`/preset/fir/enabled?preset_name=${enc(P)}&state=maybe`)).status).toBe(400)
  })
})

// ===== SD recorder / player =====
// State changes are asynchronous on the device (commands are acknowledged
// immediately, the new state arrives as recorderState broadcasts), so the
// mutation tests assert the broadcasts. Mutations are mock-only: against a
// real device they would write its SD card and lock its presets.

describe('SD recorder', () => {
  let ws

  beforeAll(async () => {
    ws = await connectWs(t.wsUrl)
  })

  afterAll(async () => {
    if (ws) ws.close()
    // Never leave a mock recording running: it would lock preset switching
    // for every later test in the suite
    await POST('/recorder/record/stop')
    await POST('/recorder/play/stop')
  })

  it('GET /recorder returns the state + files shape', async () => {
    const res = await GET('/recorder')
    expect(res.status).toBe(200)
    expect(typeof res.json.sdPresent).toBe('boolean')
    expect(typeof res.json.recording.active).toBe('boolean')
    expect(typeof res.json.playback.active).toBe('boolean')
    expect(Array.isArray(res.json.files)).toBe(true)
    for (const f of res.json.files) {
      expect(typeof f.name).toBe('string')
      expect(typeof f.size).toBe('number')
      expect(typeof f.seconds).toBe('number')
    }
  })

  itMockOnly('record start/stop broadcasts state and grows the list', async () => {
    const started = ws.expect((m) => m.messageType === 'recorderState' && m.recording.active)
    expect((await POST('/recorder/record/start')).status).toBe(200)
    const state = await started
    expect(state.recording.file).toMatch(/^rec-\d{3}\.wav$/)
    const newFile = state.recording.file

    // The lock: preset switches and deletes refuse while recording
    expect((await PUT(`/preset/active?name=${enc(P)}`)).status).toBe(409)
    expect((await DEL(`/preset?name=${enc(P)}`)).status).toBe(409)
    expect((await POST(`/recorder/play?name=${enc('rec-001.wav')}`)).status).toBe(409)
    expect((await DEL(`/recorder/file?name=${enc('rec-001.wav')}`)).status).toBe(409)

    const stopped = ws.expect((m) => m.messageType === 'recorderState' && !m.recording.active)
    const changed = ws.expect((m) => m.messageType === 'recordingsChanged')
    expect((await POST('/recorder/record/stop')).status).toBe(200)
    await stopped
    await changed

    const files = (await GET('/recorder')).json.files
    expect(files.some((f) => f.name === newFile)).toBe(true)
  })

  itMockOnly('playback runs a position and stops on demand', async () => {
    const playing = ws.expect((m) => m.messageType === 'recorderState' && m.playback.active)
    expect((await POST(`/recorder/play?name=${enc('rec-001.wav')}`)).status).toBe(200)
    const state = await playing
    expect(state.playback.file).toBe('rec-001.wav')
    expect(state.playback.length).toBeGreaterThan(0)

    const stopped = ws.expect((m) => m.messageType === 'recorderState' && !m.playback.active)
    expect((await POST('/recorder/play/stop')).status).toBe(200)
    await stopped
  })

  itMockOnly('delete removes the file and announces the change', async () => {
    // Record something disposable first
    const started = ws.expect((m) => m.messageType === 'recorderState' && m.recording.active)
    await POST('/recorder/record/start')
    const name = (await started).recording.file
    const stopped = ws.expect((m) => m.messageType === 'recorderState' && !m.recording.active)
    await POST('/recorder/record/stop')
    await stopped

    const changed = ws.expect((m) => m.messageType === 'recordingsChanged')
    expect((await DEL(`/recorder/file?name=${enc(name)}`)).status).toBe(200)
    await changed
    const files = (await GET('/recorder')).json.files
    expect(files.some((f) => f.name === name)).toBe(false)
  })

  it('missing name is 400 on play and delete', async () => {
    expect((await POST('/recorder/play')).status).toBe(400)
    expect((await DEL('/recorder/file')).status).toBe(400)
  })
})

// ===== V1: templates =====

describe('GET /templates', () => {
  it('lists the six templates with id/label/description/outputsUsed', async () => {
    const res = await GET('/templates')
    expect(res.status).toBe(200)
    expect(res.json.map((t) => t.id)).toEqual(['2.0', '2.1', '2.2', '2way-sub', '3way', '3way-2sub'])
    for (const t of res.json) {
      expect(typeof t.label).toBe('string')
      expect(typeof t.description).toBe('string')
      expect(t.outputsUsed).toBeGreaterThanOrEqual(2)
      expect(t.outputsUsed).toBeLessThanOrEqual(8)
    }
    expect(res.json.find((t) => t.id === '3way-2sub').outputsUsed).toBe(8)
  })
})

// ===== V1: preset shape =====

describe('V1 preset shape', () => {
  const FULL = `${PREFIX}-v1-full`
  // Fresh preset for shape assertions: P has been mutated by earlier suites
  const FRESH = `${PREFIX}-v1-21`

  beforeAll(async () => {
    const res = await POST(`/preset?action=create&name=${enc(FULL)}&template=3way-2sub`)
    expect(res.status).toBe(201)
    expect((await POST(`/preset?action=create&name=${enc(FRESH)}`)).status).toBe(201)
  })

  it('POST create rejects an unknown template with 400', async () => {
    expect((await POST(`/preset?action=create&name=${enc(PREFIX + '-bad')}&template=quadraphonic`)).status).toBe(400)
  })

  it('a default-template preset is 2.1: L/R over sub_xo HP, mono sub with LP', async () => {
    const preset = await getPreset(FRESH)
    expect(preset.template).toBe('2.1')
    expect(preset.outputs).toHaveLength(8)
    expect(preset.crossovers).toHaveLength(1)
    expect(preset.crossovers[0]).toMatchObject({ id: 'sub_xo', type: 'LR4', locked: false, min: 40, max: 500 })

    const [left, right, sub] = preset.outputs
    expect(left).toMatchObject({ label: 'Left', enabled: true, source: { left: 1, right: 0 }, hp: { mode: 'xover', xover: 'sub_xo' } })
    expect(right).toMatchObject({ label: 'Right', enabled: true, source: { left: 0, right: 1 }, hp: { mode: 'xover', xover: 'sub_xo' } })
    expect(sub).toMatchObject({ label: 'Sub', enabled: true, source: { left: 0.5, right: 0.5 }, lp: { mode: 'xover', xover: 'sub_xo' } })
    // Unused slots are disabled
    for (const output of preset.outputs.slice(3)) {
      expect(output.enabled).toBe(false)
    }
    // Every output carries the full field set
    for (const output of preset.outputs) {
      expect(Array.isArray(output.peq)).toBe(true)
      expect(typeof output.eqEnabled).toBe('boolean')
      expect(typeof output.fir).toBe('string')
      expect(typeof output.delayUs).toBe('number')
      expect(typeof output.gainDb).toBe('number')
      expect(typeof output.invert).toBe('boolean')
      expect(typeof output.mute).toBe('boolean')
      expect(typeof output.hpFloor).toBe('number')
    }
    expect(preset.inputEq.enabled).toBe(false)
    expect(preset.inputEq.sets.find((s) => s.spl === 0).points).toHaveLength(3)
    expect(preset.firPool).toMatchObject({ total: 12288, used: 0 })
  })

  it('a 3way-2sub preset uses all 8 outputs with locked mid/tweeter points and floors', async () => {
    const preset = await getPreset(FULL)
    expect(preset.template).toBe('3way-2sub')
    expect(preset.outputs.every((o) => o.enabled)).toBe(true)

    const ids = preset.crossovers.map((x) => x.id)
    expect(ids).toEqual(['sub_xo', 'mid_xo', 'twt_xo'])
    expect(preset.crossovers.find((x) => x.id === 'sub_xo').locked).toBe(false)
    expect(preset.crossovers.find((x) => x.id === 'mid_xo').locked).toBe(true)
    expect(preset.crossovers.find((x) => x.id === 'twt_xo').locked).toBe(true)

    // Highs are floor-protected; lows band-pass between sub_xo and mid_xo
    expect(preset.outputs[4]).toMatchObject({ label: 'L High', hp: { mode: 'xover', xover: 'twt_xo' }, hpFloor: 800 })
    expect(preset.outputs[0]).toMatchObject({ label: 'L Low', hp: { mode: 'xover', xover: 'sub_xo' }, lp: { mode: 'xover', xover: 'mid_xo' } })
    expect(preset.outputs[6]).toMatchObject({ label: 'Sub 1', source: { left: 0.5, right: 0.5 } })
  })
})

// ===== V1: locked crossover points and hpFloor =====

describe('locked crossover safety semantics', () => {
  const SAFE = `${PREFIX}-v1-safety`

  beforeAll(async () => {
    expect((await POST(`/preset?action=create&name=${enc(SAFE)}&template=2way-sub`)).status).toBe(201)
  })

  it('rejects a locked point write without confirm=true (409, locked flag set)', async () => {
    const res = await PUT(`/preset/crossover?preset_name=${enc(SAFE)}&id=twt_xo&frequency=3000`)
    expect(res.status).toBe(409)
    expect(res.json).toMatchObject({ locked: true })
    // Unchanged
    const preset = await getPreset(SAFE)
    expect(preset.crossovers.find((x) => x.id === 'twt_xo').freq).toBe(2500)
  })

  it('applies a locked point write with confirm=true', async () => {
    const res = await PUT(`/preset/crossover?preset_name=${enc(SAFE)}&id=twt_xo&frequency=3000&confirm=true`)
    expect(res.status).toBe(200)
    expect(res.json).toEqual({ messageType: 'crossoverChanged', presetName: SAFE, status: 'ok', id: 'twt_xo', crossoverFreq: 3000 })
    const preset = await getPreset(SAFE)
    expect(preset.crossovers.find((x) => x.id === 'twt_xo').freq).toBe(3000)
  })

  it('validates locked writes against the point range even with confirm', async () => {
    expect((await PUT(`/preset/crossover?preset_name=${enc(SAFE)}&id=twt_xo&frequency=100&confirm=true`)).status).toBe(400)
  })

  it('refuses to bypass a floor-protected high-pass even with confirm (409)', async () => {
    // Tweeters in 2way-sub carry hpFloor 800; disabling twt_xo would leave
    // them unprotected, so the floor blocks it regardless of confirmation
    const res = await PUT(`/preset/crossover/enabled?preset_name=${enc(SAFE)}&id=twt_xo&enabled=off&confirm=true`)
    expect(res.status).toBe(409)
    const preset = await getPreset(SAFE)
    expect(preset.outputs[2].hp.mode).toBe('xover')
  })

  it('allows bypassing and restoring the unlocked sub crossover', async () => {
    const off = await PUT(`/preset/crossover/enabled?preset_name=${enc(SAFE)}&id=sub_xo&enabled=off`)
    expect(off.status).toBe(200)
    let preset = await getPreset(SAFE)
    expect(preset.outputs[0].hp).toMatchObject({ mode: 'off', xover: 'sub_xo' })
    expect(preset.outputs[4].lp).toMatchObject({ mode: 'off', xover: 'sub_xo' })

    // Re-enabling restores the kept reference
    expect((await PUT(`/preset/crossover/enabled?preset_name=${enc(SAFE)}&id=sub_xo&enabled=on`)).status).toBe(200)
    preset = await getPreset(SAFE)
    expect(preset.outputs[0].hp).toMatchObject({ mode: 'xover', xover: 'sub_xo' })
    expect(preset.outputs[4].lp).toMatchObject({ mode: 'xover', xover: 'sub_xo' })
  })

  it('rejects a per-output HP edit that would sink below the floor (409)', async () => {
    // Output 2 is L Tweeter (hpFloor 800): manual 500 Hz is below the floor
    const below = await PUT(`/preset/output/filter?preset_name=${enc(SAFE)}&output=2&which=hp`, { mode: 'manual', freq: 500, type: 'LR4' })
    expect(below.status).toBe(409)
    // ...but a manual HP above the floor is the power-user path and is fine
    const above = await PUT(`/preset/output/filter?preset_name=${enc(SAFE)}&output=2&which=hp`, { mode: 'manual', freq: 900, type: 'LR4' })
    expect(above.status).toBe(200)
    // Turning the HP off entirely is also blocked by the floor
    const off = await PUT(`/preset/output/filter?preset_name=${enc(SAFE)}&output=2&which=hp`, { mode: 'off' })
    expect(off.status).toBe(409)
  })
})

// ===== V1: output channels =====

describe('output channel endpoints', () => {
  it('rejects a missing or out-of-range output index with 400', async () => {
    expect((await PUT(`/preset/output/gain?preset_name=${enc(P)}&value=0`)).status).toBe(400)
    expect((await PUT(`/preset/output/gain?preset_name=${enc(P)}&output=8&value=0`)).status).toBe(400)
    expect((await PUT(`/preset/output/gain?preset_name=${enc(P)}&output=1.5&value=0`)).status).toBe(400)
  })

  it('404s for an unknown preset', async () => {
    expect((await PUT(`/preset/output/gain?preset_name=${enc(PREFIX + '-missing')}&output=0&value=0`)).status).toBe(404)
  })

  it('sets gain in dB and clamps into -40..+10', async () => {
    const res = await PUT(`/preset/output/gain?preset_name=${enc(P)}&output=0&value=-2.5`)
    expect(res.status).toBe(200)
    expect(res.json).toMatchObject({ messageType: 'outputChanged', output: 0, changes: { gainDb: -2.5 } })
    expect((await getPreset(P)).outputs[0].gainDb).toBeCloseTo(-2.5, 3)

    await PUT(`/preset/output/gain?preset_name=${enc(P)}&output=0&value=99`)
    expect((await getPreset(P)).outputs[0].gainDb).toBeCloseTo(10, 3)
    await PUT(`/preset/output/gain?preset_name=${enc(P)}&output=0&value=0`)
  })

  it('sets mute, invert and enabled as on/off states', async () => {
    await PUT(`/preset/output/mute?preset_name=${enc(P)}&output=2&state=on`)
    await PUT(`/preset/output/invert?preset_name=${enc(P)}&output=2&state=on`)
    await PUT(`/preset/output/enabled?preset_name=${enc(P)}&output=7&state=on`)
    let preset = await getPreset(P)
    expect(preset.outputs[2].mute).toBe(true)
    expect(preset.outputs[2].invert).toBe(true)
    expect(preset.outputs[7].enabled).toBe(true)

    await PUT(`/preset/output/mute?preset_name=${enc(P)}&output=2&state=off`)
    await PUT(`/preset/output/invert?preset_name=${enc(P)}&output=2&state=off`)
    await PUT(`/preset/output/enabled?preset_name=${enc(P)}&output=7&state=off`)
    preset = await getPreset(P)
    expect(preset.outputs[2].mute).toBe(false)
    expect(preset.outputs[2].invert).toBe(false)
    expect(preset.outputs[7].enabled).toBe(false)

    expect((await PUT(`/preset/output/mute?preset_name=${enc(P)}&output=2&state=maybe`)).status).toBe(400)
  })

  it('toggles the output EQ bypass, replying with the outputChanged shape', async () => {
    const off = await PUT(`/preset/output/eq/enabled?preset_name=${enc(P)}&output=2&state=off`)
    expect(off.status).toBe(200)
    expect(off.json).toMatchObject({ messageType: 'outputChanged', output: 2, changes: { eqEnabled: false } })
    expect((await getPreset(P)).outputs[2].eqEnabled).toBe(false)

    await PUT(`/preset/output/eq/enabled?preset_name=${enc(P)}&output=2&state=on`)
    expect((await getPreset(P)).outputs[2].eqEnabled).toBe(true)

    expect((await PUT(`/preset/output/eq/enabled?preset_name=${enc(P)}&output=2&state=maybe`)).status).toBe(400)
  })

  it('sets the label and rejects empty/oversized ones', async () => {
    const res = await PUT(`/preset/output/label?preset_name=${enc(P)}&output=0&label=${enc('L Woofer')}`)
    expect(res.status).toBe(200)
    expect((await getPreset(P)).outputs[0].label).toBe('L Woofer')
    expect((await PUT(`/preset/output/label?preset_name=${enc(P)}&output=0&label=`)).status).toBe(400)
    expect((await PUT(`/preset/output/label?preset_name=${enc(P)}&output=0&label=${enc('x'.repeat(25))}`)).status).toBe(400)
    await PUT(`/preset/output/label?preset_name=${enc(P)}&output=0&label=Left`)
  })

  it('sets the source mix from a JSON body, clamping into 0..1', async () => {
    const res = await PUT(`/preset/output/source?preset_name=${enc(P)}&output=2`, { left: 0.7, right: 1.5 })
    expect(res.status).toBe(200)
    expect((await getPreset(P)).outputs[2].source).toEqual({ left: 0.7, right: 1 })
    expect((await PUT(`/preset/output/source?preset_name=${enc(P)}&output=2`, { left: 'x' })).status).toBe(400)
    await PUT(`/preset/output/source?preset_name=${enc(P)}&output=2`, { left: 0.5, right: 0.5 })
  })

  it('sets the delay in microseconds within 0..20000', async () => {
    const res = await PUT(`/preset/output/delay?preset_name=${enc(P)}&output=1&value=1234.5`)
    expect(res.status).toBe(200)
    expect((await getPreset(P)).outputs[1].delayUs).toBeCloseTo(1234.5, 3)
    expect((await PUT(`/preset/output/delay?preset_name=${enc(P)}&output=1&value=20001`)).status).toBe(400)
    expect((await PUT(`/preset/output/delay?preset_name=${enc(P)}&output=1&value=-1`)).status).toBe(400)
    await PUT(`/preset/output/delay?preset_name=${enc(P)}&output=1&value=0`)
  })

  it('sets HP/LP sections: manual mode, xover references, validation', async () => {
    // Manual LP on the sub output
    const manual = await PUT(`/preset/output/filter?preset_name=${enc(P)}&output=2&which=lp`, { mode: 'manual', freq: 90, type: 'BW2' })
    expect(manual.status).toBe(200)
    expect((await getPreset(P)).outputs[2].lp).toEqual({ mode: 'manual', freq: 90, type: 'BW2' })

    // Back to referencing the shared point
    const ref = await PUT(`/preset/output/filter?preset_name=${enc(P)}&output=2&which=lp`, { mode: 'xover', xover: 'sub_xo' })
    expect(ref.status).toBe(200)
    expect((await getPreset(P)).outputs[2].lp).toEqual({ mode: 'xover', xover: 'sub_xo' })

    expect((await PUT(`/preset/output/filter?preset_name=${enc(P)}&output=2&which=lp`, { mode: 'xover', xover: 'nope_xo' })).status).toBe(400)
    expect((await PUT(`/preset/output/filter?preset_name=${enc(P)}&output=2&which=lp`, { mode: 'manual', freq: 10 })).status).toBe(400)
    expect((await PUT(`/preset/output/filter?preset_name=${enc(P)}&output=2&which=lp`, { mode: 'manual', freq: 100, type: 'CHEBY9' })).status).toBe(400)
    expect((await PUT(`/preset/output/filter?preset_name=${enc(P)}&output=2&which=zp`, { mode: 'off' })).status).toBe(400)
  })

  it('saves an output PEQ array (204) capped at 10 points', async () => {
    const points = [
      { freq: 45, gain: -3, q: 4 },
      { freq: 120, gain: 2, q: 1 },
    ]
    const res = await PUT(`/preset/output/eq?preset_name=${enc(P)}&output=2`, points)
    expect(res.status).toBe(204)

    const peq = (await getPreset(P)).outputs[2].peq
    expect(peq).toHaveLength(2)
    expect(peq[0].freq).toBeCloseTo(45, 3)

    const tooMany = Array.from({ length: 11 }, () => ({ freq: 1000, gain: 0, q: 1 }))
    expect((await PUT(`/preset/output/eq?preset_name=${enc(P)}&output=2`, tooMany)).status).toBe(400)
  })

  it('updates/appends single output PEQ points with gap protection', async () => {
    await PUT(`/preset/output/eq?preset_name=${enc(P)}&output=2`, [{ freq: 45, gain: -3, q: 4 }])
    // Update in place
    expect((await PUT(`/preset/output/eq/point?preset_name=${enc(P)}&output=2`, { id: 0, freq: 50, gain: -4, q: 4 })).status).toBe(204)
    // Append directly after the last point
    expect((await PUT(`/preset/output/eq/point?preset_name=${enc(P)}&output=2`, { id: 1, freq: 80, gain: 1, q: 2 })).status).toBe(204)
    const peq = (await getPreset(P)).outputs[2].peq
    expect(peq).toHaveLength(2)
    expect(peq[0].freq).toBeCloseTo(50, 3)
    expect(peq[1].freq).toBeCloseTo(80, 3)
    // Gap and bounds rejections
    expect((await PUT(`/preset/output/eq/point?preset_name=${enc(P)}&output=2`, { id: 3, freq: 100, gain: 0, q: 1 })).status).toBe(400)
    expect((await PUT(`/preset/output/eq/point?preset_name=${enc(P)}&output=2`, { id: 10, freq: 100, gain: 0, q: 1 })).status).toBe(400)
  })
})

// ===== V1: template flips to custom on structural edits =====

describe('template custom-flip', () => {
  const FLIP = `${PREFIX}-v1-flip`

  beforeAll(async () => {
    expect((await POST(`/preset?action=create&name=${enc(FLIP)}`)).status).toBe(201)
  })

  it('tuning edits (gain, delay, PEQ, FIR, mute) keep the template', async () => {
    await PUT(`/preset/output/gain?preset_name=${enc(FLIP)}&output=0&value=-3`)
    await PUT(`/preset/output/delay?preset_name=${enc(FLIP)}&output=1&value=500`)
    await PUT(`/preset/output/mute?preset_name=${enc(FLIP)}&output=2&state=on`)
    await PUT(`/preset/output/eq?preset_name=${enc(FLIP)}&output=0`, [{ freq: 100, gain: 1, q: 1 }])
    await PUT(`/preset/output/fir?preset_name=${enc(FLIP)}&output=0&file=fir_flat.txt`)
    expect((await getPreset(FLIP)).template).toBe('2.1')
  })

  it('a structural edit flips the template to custom and reports it', async () => {
    const res = await PUT(`/preset/output/source?preset_name=${enc(FLIP)}&output=0`, { left: 0.8, right: 0.2 })
    expect(res.status).toBe(200)
    expect(res.json.template).toBe('custom')
    expect((await getPreset(FLIP)).template).toBe('custom')

    // Subsequent structural edits stay custom without re-reporting
    const again = await PUT(`/preset/output/enabled?preset_name=${enc(FLIP)}&output=7&state=on`)
    expect(again.json.template).toBeUndefined()
    expect((await getPreset(FLIP)).template).toBe('custom')
  })
})

// ===== V1: FIR tap pool =====

describe('FIR tap pool', () => {
  const POOL = `${PREFIX}-v1-pool`

  beforeAll(async () => {
    expect((await POST(`/preset?action=create&name=${enc(POOL)}&template=3way-2sub`)).status).toBe(201)
  })

  it('GET /preset/fir/pool reports total, used and per-output taps', async () => {
    const res = await GET(`/preset/fir/pool?preset_name=${enc(POOL)}`)
    expect(res.status).toBe(200)
    expect(res.json.total).toBe(12288)
    expect(res.json.used).toBe(0)
    expect(res.json.outputs).toHaveLength(8)
    for (const o of res.json.outputs) {
      expect(o).toMatchObject({ file: '', taps: 0 })
    }
  })

  it('tracks tap usage as files load and rejects loads that exceed the pool', async () => {
    // The mock's tap map: room1/room2 = 4096, speaker1/speaker2 = 2048
    expect((await PUT(`/preset/output/fir?preset_name=${enc(POOL)}&output=0&file=fir_room1.txt`)).status).toBe(200)
    expect((await PUT(`/preset/output/fir?preset_name=${enc(POOL)}&output=1&file=fir_room2.txt`)).status).toBe(200)
    expect((await PUT(`/preset/output/fir?preset_name=${enc(POOL)}&output=2&file=fir_speaker1.txt`)).status).toBe(200)
    const almostFull = await PUT(`/preset/output/fir?preset_name=${enc(POOL)}&output=3&file=fir_speaker2.txt`)
    expect(almostFull.status).toBe(200)
    expect(almostFull.json.firPool).toEqual({ total: 12288, used: 12288 })

    // Pool is exactly full: one more load must be rejected with the usage
    const overflow = await PUT(`/preset/output/fir?preset_name=${enc(POOL)}&output=4&file=fir_flat.txt`)
    expect(overflow.status).toBe(409)
    expect(overflow.json).toMatchObject({ total: 12288 })
    expect(overflow.json.used).toBeGreaterThan(12288)
    expect((await getPreset(POOL)).outputs[4].fir).toBe('')

    // Clearing a file frees its taps and the load succeeds
    expect((await PUT(`/preset/output/fir?preset_name=${enc(POOL)}&output=0&file=`)).status).toBe(200)
    expect((await PUT(`/preset/output/fir?preset_name=${enc(POOL)}&output=4&file=fir_flat.txt`)).status).toBe(200)
    const pool = (await GET(`/preset/fir/pool?preset_name=${enc(POOL)}`)).json
    expect(pool.used).toBe(12288 - 4096 + 1024)
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

  it('volumeChanged names the preset the level belongs to', async () => {
    const current = (await GET('/status')).json.currentPreset
    const wait = ws.expect((m) => m.messageType === 'volumeChanged')
    await PUT('/volume?value=47')
    expect(await wait).toEqual({ messageType: 'volumeChanged', presetName: current, volume: 47 })
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

  it('activePresetChanged carries the name, index and the new master volume', async () => {
    const wait = ws.expect((m) => m.messageType === 'activePresetChanged' && m.activePresetName === P)
    await PUT(`/preset/active?name=${enc(P)}`)
    const msg = await wait
    expect(msg.activePresetName).toBe(P)
    expect(typeof msg.activePresetIndex).toBe('number')
    // Per-preset master volume: clients follow the switch without refetching
    expect(msg.volume).toBe((await getPreset(P)).volume)

    // Restore the original active preset (also a broadcast; consume it)
    const waitRestore = ws.expect((m) => m.messageType === 'activePresetChanged')
    await PUT(`/preset/active?name=${enc(snapshot.currentPreset)}`)
    await waitRestore
  })

  it('crossoverChanged', async () => {
    const wait = ws.expect((m) => m.messageType === 'crossoverChanged' && m.presetName === P)
    await PUT(`/preset/crossover?preset_name=${enc(P)}&id=sub_xo&frequency=95`)
    expect(await wait).toEqual({ messageType: 'crossoverChanged', presetName: P, status: 'ok', id: 'sub_xo', crossoverFreq: 95 })
  })

  it('probeEvent START on probe start, STOP on cancel', async () => {
    const waitStart = ws.expect((m) => m.messageType === 'probeEvent' && m.line.startsWith('START'))
    await PUT('/probe/delay/start?level=30')
    const started = await waitStart
    // "START <mask> <nChirps> <preRoll> <spacing> <chirpLen>"
    expect(started.line.split(' ')).toHaveLength(6)

    const waitStop = ws.expect((m) => m.messageType === 'probeEvent' && m.line === 'STOP')
    await PUT('/probe/delay/stop')
    await waitStop
  })

  it('crossoverEnabledChanged', async () => {
    const wait = ws.expect((m) => m.messageType === 'crossoverEnabledChanged' && m.presetName === P)
    await PUT(`/preset/crossover/enabled?preset_name=${enc(P)}&id=sub_xo&enabled=off`)
    expect(await wait).toEqual({ messageType: 'crossoverEnabledChanged', presetName: P, status: 'ok', id: 'sub_xo', crossoverEnabled: false })
  })

  it('outputChanged carries the output index and the changed fields', async () => {
    const wait = ws.expect((m) => m.messageType === 'outputChanged' && m.presetName === P)
    await PUT(`/preset/output/gain?preset_name=${enc(P)}&output=1&value=-3`)
    expect(await wait).toEqual({
      messageType: 'outputChanged',
      presetName: P,
      status: 'ok',
      output: 1,
      changes: { gainDb: -3 },
    })
  })

  it('outputEqChanged reports the output and point count', async () => {
    const wait = ws.expect((m) => m.messageType === 'outputEqChanged' && m.presetName === P)
    await PUT(`/preset/output/eq?preset_name=${enc(P)}&output=2`, [{ freq: 60, gain: -4, q: 3 }])
    expect(await wait).toEqual({
      messageType: 'outputEqChanged',
      presetName: P,
      status: 'ok',
      output: 2,
      numPoints: 1,
    })
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
