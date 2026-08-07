import { describe, it, expect } from 'vitest'
import {
  generateChirp,
  correlationEnvelope,
  findArrivals,
  computeDelays,
  analyzeRecording,
  usToCm,
  MIN_CONFIDENCE,
} from '../../src/delay-align.js'

// A shrunken but structurally identical schedule keeps the FFTs small and
// the tests fast; the analysis code is fully parameterized by it.
const SCHEDULE = {
  sampleRate: 44100,
  preRollSamples: 16384,
  spacingSamples: 12288,
  chirpSamples: 4096,
  tailSamples: 2048,
  fadeSamples: 256,
  f0: 100,
  f1: 8000,
}

const PHONE_RATE = 48000
const MAX_DELAY_US = 20000

// Deterministic pseudo-noise (mulberry32) so failures reproduce.
function noiseGen(seed) {
  let a = seed
  return () => {
    a |= 0
    a = (a + 0x6d2b79f5) | 0
    let t = Math.imul(a ^ (a >>> 15), 1 | a)
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t
    return (((t ^ (t >>> 14)) >>> 0) / 4294967296) * 2 - 1
  }
}

/*
 * Synthesize a phone recording of a probe run: the chirp of slot k starts
 * at anchor + k*spacing (converted to the phone rate) plus that slot's
 * extra acoustic flight time. amplitude<0 flips polarity (inverted output).
 */
function synthesizeRecording({
  order,
  slotDeviationsUs,
  anchorSample = 5000,
  amplitudes = null,
  noiseAmp = 0.02,
  seed = 42,
}) {
  const chirp = generateChirp(PHONE_RATE, SCHEDULE)
  const perSample = PHONE_RATE / SCHEDULE.sampleRate
  const lastStart =
    anchorSample + Math.round((order.length - 1) * SCHEDULE.spacingSamples * perSample)
  const length = lastStart + chirp.length + Math.round(0.3 * PHONE_RATE)
  const rec = new Float32Array(length)

  const rand = noiseGen(seed)
  for (let i = 0; i < length; i++) rec[i] = noiseAmp * rand()

  order.forEach((output, slot) => {
    const amp = amplitudes ? amplitudes[slot] : 0.5
    if (amp === 0) return // silent output (unrouted / disconnected)
    const start =
      anchorSample +
      Math.round(
        slot * SCHEDULE.spacingSamples * perSample +
          (slotDeviationsUs[slot] / 1e6) * PHONE_RATE
      )
    for (let i = 0; i < chirp.length; i++) {
      rec[start + i] += amp * chirp[i]
    }
  })
  return rec
}

describe('generateChirp', () => {
  it('scales length with sample rate and fades to silence at both ends', () => {
    const at44 = generateChirp(44100, SCHEDULE)
    const at48 = generateChirp(48000, SCHEDULE)
    expect(at44.length).toBe(SCHEDULE.chirpSamples)
    expect(at48.length).toBe(Math.round((SCHEDULE.chirpSamples * 48000) / 44100))
    expect(Math.abs(at48[0])).toBeLessThan(1e-6)
    expect(Math.abs(at48[at48.length - 1])).toBeLessThan(0.02)
    // Real sweep energy in the middle
    const mid = at48.slice(1000, 3000)
    expect(Math.max(...mid.map(Math.abs))).toBeGreaterThan(0.9)
  })
})

describe('correlationEnvelope', () => {
  it('peaks at the embedded chirp offset', () => {
    const chirp = generateChirp(PHONE_RATE, SCHEDULE)
    const rec = new Float32Array(20000 + chirp.length + 8000)
    for (let i = 0; i < chirp.length; i++) rec[20000 + i] = 0.4 * chirp[i]
    const env = correlationEnvelope(rec, chirp)
    let peak = 0
    for (let t = 1; t < rec.length - chirp.length; t++) {
      if (env[t] > env[peak]) peak = t
    }
    expect(Math.abs(peak - 20000)).toBeLessThanOrEqual(1)
  })
})

describe('findArrivals + computeDelays (synthetic recordings)', () => {
  it('recovers per-output offsets within 50us on a clean two-output probe', () => {
    // Output 1 arrives 1250us late (~43cm farther away)
    const order = [0, 1, 1, 0]
    const rec = synthesizeRecording({ order, slotDeviationsUs: [0, 1250, 1250, 0] })
    const result = analyzeRecording(
      rec, PHONE_RATE, { ...SCHEDULE, order }, [0, 0], MAX_DELAY_US
    )
    const [ch0, ch1] = result.channels
    expect(ch0.measured).toBe(true)
    expect(ch1.measured).toBe(true)
    expect(ch1.offsetUs - ch0.offsetUs).toBeCloseTo(1250, -2) // within ~50us
    // The early output gets delayed to match; the late one sits at 0
    expect(ch0.newDelayUs).toBeGreaterThan(1150)
    expect(ch0.newDelayUs).toBeLessThan(1350)
    expect(ch1.newDelayUs).toBe(0)
    expect(result.spreadUs).toBeGreaterThan(1150)
  })

  it('cancels linear phone-clock drift via forward/reverse averaging', () => {
    // 200ppm drift (4x a bad phone crystal): each slot slides progressively
    // later. Forward+reverse averaging must still recover the true offset.
    const order = [0, 1, 1, 0]
    const spacingS = SCHEDULE.spacingSamples / SCHEDULE.sampleRate
    const drift = 200e-6
    const devs = order.map((_, slot) => slot * spacingS * drift * 1e6)
    devs[1] += 800 // true offset of output 1
    devs[2] += 800
    const rec = synthesizeRecording({ order, slotDeviationsUs: devs })
    const result = analyzeRecording(
      rec, PHONE_RATE, { ...SCHEDULE, order }, [0, 0], MAX_DELAY_US
    )
    const [ch0, ch1] = result.channels
    // Slots 0+3 and 1+2 straddle the same midpoint, so the drift term
    // averages to the same constant for both outputs and cancels in the
    // difference.
    expect(ch1.offsetUs - ch0.offsetUs).toBeCloseTo(800, -2)
  })

  it('marks a silent output as not measured and still aligns the rest', () => {
    const order = [0, 1, 2, 2, 1, 0]
    const rec = synthesizeRecording({
      order,
      slotDeviationsUs: [0, 500, 0, 0, 500, 0],
      amplitudes: [0.5, 0.5, 0, 0, 0.5, 0.5], // output 2 never plays
    })
    const result = analyzeRecording(
      rec, PHONE_RATE, { ...SCHEDULE, order }, [0, 0, 0], MAX_DELAY_US
    )
    const ch2 = result.channels.find((c) => c.output === 2)
    expect(ch2.measured).toBe(false)
    expect(ch2.newDelayUs).toBe(null)
    const ch0 = result.channels.find((c) => c.output === 0)
    const ch1 = result.channels.find((c) => c.output === 1)
    expect(ch1.offsetUs - ch0.offsetUs).toBeCloseTo(500, -2)
    expect(ch0.newDelayUs).toBeGreaterThan(400)
    expect(ch1.newDelayUs).toBe(0)
  })

  it('detects an inverted output (envelope is polarity-blind)', () => {
    const order = [0, 1, 1, 0]
    const rec = synthesizeRecording({
      order,
      slotDeviationsUs: [0, 300, 300, 0],
      amplitudes: [0.5, -0.5, -0.5, 0.5],
    })
    const result = analyzeRecording(
      rec, PHONE_RATE, { ...SCHEDULE, order }, [0, 0], MAX_DELAY_US
    )
    const ch1 = result.channels.find((c) => c.output === 1)
    expect(ch1.measured).toBe(true)
    expect(ch1.offsetUs - result.channels[0].offsetUs).toBeCloseTo(300, -2)
  })

  it('applies incremental correction on top of existing delays', () => {
    // Output 0 already has 1000us of delay dialed in and the probe ran with
    // it active; the residual misalignment is only 250us.
    const order = [0, 1, 1, 0]
    const rec = synthesizeRecording({ order, slotDeviationsUs: [250, 0, 0, 250] })
    const result = analyzeRecording(
      rec, PHONE_RATE, { ...SCHEDULE, order }, [1000, 0], MAX_DELAY_US
    )
    const [ch0, ch1] = result.channels
    // ch0 arrives 250us late: ch1 must be delayed to 1000+250 minus the
    // min-normalization against ch0's 1000 -> ch0: 750... walk it through:
    // latest = ch0. raw ch0 = 1000+0 = 1000, raw ch1 = 0+250 = 250.
    // min = 250 -> ch0 = 750, ch1 = 0.
    expect(ch0.newDelayUs).toBeGreaterThan(650)
    expect(ch0.newDelayUs).toBeLessThan(850)
    expect(ch1.newDelayUs).toBe(0)
  })
})

describe('computeDelays (direct)', () => {
  const arrival = (deviationS, detected = true) => ({
    sample: 0,
    deviationS,
    confidence: detected ? 10 : 1,
    detected,
  })

  it('clamps un-alignable spreads at the device cap and flags them', () => {
    const order = [0, 1, 1, 0]
    // Output 1 arrives 25ms late - beyond the 20ms delay cap
    const arrivals = [arrival(0), arrival(0.025), arrival(0.025), arrival(0)]
    const res = computeDelays(arrivals, order, [0, 0], MAX_DELAY_US)
    const ch0 = res.find((c) => c.output === 0)
    expect(ch0.newDelayUs).toBe(MAX_DELAY_US)
    expect(ch0.clamped).toBe(true)
  })

  it('reports forward/reverse consistency', () => {
    const order = [0, 1, 1, 0]
    const arrivals = [arrival(0), arrival(0.001), arrival(0.0014), arrival(0)]
    const res = computeDelays(arrivals, order, [0, 0], MAX_DELAY_US)
    const ch1 = res.find((c) => c.output === 1)
    expect(ch1.consistencyUs).toBeCloseTo(400, 0)
    expect(ch1.offsetUs).toBeCloseTo(1200, 0)
  })

  it('computes no delays when fewer than two outputs measured', () => {
    const order = [0, 1, 1, 0]
    const arrivals = [arrival(0), arrival(0, false), arrival(0, false), arrival(0)]
    const res = computeDelays(arrivals, order, [0, 0], MAX_DELAY_US)
    expect(res.every((c) => c.newDelayUs === null)).toBe(true)
  })
})

describe('usToCm', () => {
  it('converts via the speed of sound', () => {
    expect(usToCm(1000)).toBeCloseTo(34.3, 1)
  })
})
