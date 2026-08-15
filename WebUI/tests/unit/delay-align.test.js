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

// The subwoofer case needs the device's real chirp length and 60Hz start:
// the correlation lobe of a low-passed output is set by the bandwidth it
// reproduces, and a shortened sweep does not reproduce the real peak-to-
// background ratios. Spacing is tightened to just clear the chirp so the
// FFT stays manageable.
const SUB_SCHEDULE = {
  ...SCHEDULE,
  spacingSamples: 20480,
  chirpSamples: 16384,
  fadeSamples: 512,
  f0: 60,
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

// Cascaded one-pole low-pass: a crude stand-in for a subwoofer's crossover.
// Four poles roll off at ~24dB/oct, so a slot filtered this way reproduces
// only the bottom of the sweep - the narrowband case that broadens the
// correlation lobe.
function lowPass(sig, fc, rate, poles = 4) {
  const dt = 1 / rate
  const a = dt / (1 / (2 * Math.PI * fc) + dt)
  const out = Float32Array.from(sig)
  for (let p = 0; p < poles; p++) {
    let y = 0
    for (let i = 0; i < out.length; i++) {
      y += a * (out[i] - y)
      out[i] = y
    }
  }
  return out
}

/*
 * Synthesize a phone recording of a probe run: the chirp of slot k starts
 * at anchor + k*spacing (converted to the phone rate) plus that slot's
 * extra acoustic flight time. amplitude<0 flips polarity (inverted output);
 * bandLimitHz[k] low-passes the slot, standing in for a crossover.
 */
function synthesizeRecording({
  order,
  slotDeviationsUs,
  anchorSample = 5000,
  amplitudes = null,
  bandLimitHz = null,
  noiseAmp = 0.02,
  seed = 42,
  schedule = SCHEDULE,
}) {
  const chirp = generateChirp(PHONE_RATE, schedule)
  const perSample = PHONE_RATE / schedule.sampleRate
  const lastStart =
    anchorSample + Math.round((order.length - 1) * schedule.spacingSamples * perSample)
  const length = lastStart + chirp.length + Math.round(0.3 * PHONE_RATE)
  const rec = new Float32Array(length)

  const rand = noiseGen(seed)
  for (let i = 0; i < length; i++) rec[i] = noiseAmp * rand()

  order.forEach((output, slot) => {
    const amp = amplitudes ? amplitudes[slot] : 0.5
    if (amp === 0) return // silent output (unrouted / disconnected)
    const fc = bandLimitHz ? bandLimitHz[slot] : null
    const wave = fc ? lowPass(chirp, fc, PHONE_RATE) : chirp
    const start =
      anchorSample +
      Math.round(
        slot * schedule.spacingSamples * perSample +
          (slotDeviationsUs[slot] / 1e6) * PHONE_RATE
      )
    for (let i = 0; i < wave.length; i++) {
      rec[start + i] += amp * wave[i]
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

  it('detects a low-passed subwoofer despite its broad correlation lobe', () => {
    // Output 1 is a sub crossed at 120Hz, so it reproduces only 60-120Hz of
    // the sweep and its correlation peak spreads over ~15ms rather than a
    // fraction of a millisecond. Scored against a fixed guard that lobe's own
    // skirts count as its background, which pins confidence just under
    // MIN_CONFIDENCE no matter how clean the recording is - the sub then
    // never detects. It arrives 4ms late (crossover group delay plus
    // distance), which makes it the acoustically latest output.
    const order = [0, 1, 1, 0]
    const rec = synthesizeRecording({
      order,
      schedule: SUB_SCHEDULE,
      slotDeviationsUs: [0, 4000, 4000, 0],
      bandLimitHz: [null, 120, 120, null],
    })
    const result = analyzeRecording(
      rec, PHONE_RATE, { ...SUB_SCHEDULE, order }, [0, 0], MAX_DELAY_US
    )
    const [ch0, ch1] = result.channels
    expect(ch1.measured).toBe(true)
    expect(ch1.confidence).toBeGreaterThan(MIN_CONFIDENCE)

    // The crossover's own group delay is folded into the measured arrival -
    // correctly so, since it really is part of when the sub's sound leaves
    // the box. Difference against an otherwise identical run with no extra
    // flight time to isolate the part the probe is supposed to recover.
    const base = analyzeRecording(
      synthesizeRecording({
        order,
        schedule: SUB_SCHEDULE,
        slotDeviationsUs: [0, 0, 0, 0],
        bandLimitHz: [null, 120, 120, null],
      }),
      PHONE_RATE, { ...SUB_SCHEDULE, order }, [0, 0], MAX_DELAY_US
    )
    const injectedUs =
      ch1.offsetUs - ch0.offsetUs -
      (base.channels[1].offsetUs - base.channels[0].offsetUs)
    // Low-frequency timing is inherently coarse: a ~150Hz arrival can only be
    // placed to within a fraction of its own period. That is still a small
    // fraction of the wavelength being aligned.
    expect(injectedUs).toBeCloseTo(4000, -3) // within ~500us

    // The latest output sits at zero and the earlier one is delayed up to it.
    expect(ch1.newDelayUs).toBe(0)
    expect(ch0.newDelayUs).toBeCloseTo(ch1.offsetUs - ch0.offsetUs, -2)
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
