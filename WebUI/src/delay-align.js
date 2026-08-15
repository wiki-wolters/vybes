/*
 * Auto delay alignment analysis: the device (ProbeSource on the Teensy)
 * plays one log chirp per enabled output - outputs ascending, then the same
 * list reversed - at exact sample offsets on its own 44.1kHz clock. The
 * phone records the whole sequence in one take, so network/Bluetooth/start
 * latency is common to every chirp and cancels; only the deviations of the
 * inter-chirp gaps from the known spacing survive, and those are the
 * relative acoustic flight times of the outputs.
 *
 * Pipeline: matched-filter the recording against an analytically generated
 * reference chirp (FFT cross-correlation, analytic envelope), find the
 * global anchor that best aligns the expected chirp schedule, pick each
 * slot's arrival with sub-sample parabolic interpolation, average each
 * output's forward+reverse slots (cancels linear phone-clock drift), then
 * turn the offsets into per-output delayUs corrections.
 *
 * The chirp/schedule contract comes from the /probe/delay/start response
 * and matches PROBE_* in ESP/esp-web-server/teensy_protocol.h.
 */

export const SPEED_OF_SOUND_M_PER_S = 343;

// Arrival peaks below this peak-to-background ratio count as "not detected"
// (an unrouted output, a disconnected speaker, or a too-quiet probe).
export const MIN_CONFIDENCE = 4;

// Search half-window around each slot's expected arrival. Generous enough
// for room-scale path differences plus phone clock drift over the sequence.
const SEARCH_WINDOW_S = 0.04;

// The noise floor is estimated over a wider half-window than the search,
// because a narrowband output's correlation lobe can span the search window
// on its own. Capped to a fraction of the chirp spacing so the estimate never
// reaches a neighbouring slot's peak.
const BACKGROUND_WINDOW_S = 0.15;
const BACKGROUND_SPACING_FRACTION = 0.4;

// The peak's own main lobe is excluded from that noise floor, and the width
// to exclude is set by the bandwidth the output actually reproduces: a
// full-range channel returns the whole 60Hz-8kHz sweep and peaks inside a
// millisecond, while a subwoofer low-passed at ~150Hz returns well under an
// octave and spreads its peak over tens of milliseconds. A fixed guard leaves
// that lobe's own skirts inside the background term, inflating it until a
// perfectly good arrival scores below MIN_CONFIDENCE - which is why
// low-passed outputs never detected. The envelope's -6dB half-width sits near
// 0.6/BW and its first null near 1/BW, so excluding twice the measured
// half-width clears the lobe at any bandwidth.
const LOBE_EDGE = 0.5;
const LOBE_GUARD = 2;
const MIN_GUARD_S = 0.003;
const MAX_GUARD_FRACTION = 0.75;

// --- Reference chirp ---

// Generate the reference chirp at an arbitrary sample rate. The device
// definition is a per-sample recurrence at 44.1kHz; at another rate the
// sweep keeps the same duration, band and fade fractions. Any sub-sample
// mismatch against the device waveform is identical for every chirp, so it
// cancels in the arrival-time differences.
export function generateChirp(sampleRate, schedule) {
  const n = Math.round(schedule.chirpSamples * sampleRate / schedule.sampleRate);
  const fade = Math.round(schedule.fadeSamples * sampleRate / schedule.sampleRate);
  const ratio = Math.exp(Math.log(schedule.f1 / schedule.f0) / n);
  const out = new Float32Array(n);
  let phase = 0;
  let freq = schedule.f0;
  for (let i = 0; i < n; i++) {
    let w = 1;
    if (i < fade) {
      w = 0.5 * (1 - Math.cos(Math.PI * i / fade));
    } else if (i > n - 1 - fade) {
      w = 0.5 * (1 - Math.cos(Math.PI * (n - 1 - i) / fade));
    }
    out[i] = w * Math.sin(phase);
    phase += 2 * Math.PI * freq / sampleRate;
    if (phase >= 2 * Math.PI) phase -= 2 * Math.PI;
    freq *= ratio;
  }
  return out;
}

// --- FFT (iterative radix-2, in-place, complex) ---

function fft(re, im, inverse) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      let t = re[i]; re[i] = re[j]; re[j] = t;
      t = im[i]; im[i] = im[j]; im[j] = t;
    }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = (inverse ? 2 : -2) * Math.PI / len;
    const wRe = Math.cos(ang);
    const wIm = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let curRe = 1;
      let curIm = 0;
      for (let k = 0; k < len / 2; k++) {
        const a = i + k;
        const b = i + k + len / 2;
        const tRe = re[b] * curRe - im[b] * curIm;
        const tIm = re[b] * curIm + im[b] * curRe;
        re[b] = re[a] - tRe;
        im[b] = im[a] - tIm;
        re[a] += tRe;
        im[a] += tIm;
        const nRe = curRe * wRe - curIm * wIm;
        curIm = curRe * wIm + curIm * wRe;
        curRe = nRe;
      }
    }
  }
  if (inverse) {
    for (let i = 0; i < n; i++) {
      re[i] /= n;
      im[i] /= n;
    }
  }
}

// Cross-correlation envelope of recording against the reference chirp:
// |analytic(corr)|, where corr[t] = sum_n recording[t+n] * chirp[n]. The
// analytic envelope (negative frequencies zeroed on the inverse transform)
// is smooth through the peak, so parabolic interpolation is unbiased - and
// it is polarity-blind, so inverted outputs still peak. Returned length
// matches the recording; only t <= recording.length - chirp.length is
// meaningful.
export function correlationEnvelope(recording, chirp) {
  let size = 1;
  while (size < recording.length + chirp.length) size <<= 1;
  const re = new Float32Array(size);
  const im = new Float32Array(size);
  re.set(recording);
  fft(re, im, false);
  const hRe = new Float32Array(size);
  const hIm = new Float32Array(size);
  hRe.set(chirp);
  fft(hRe, hIm, false);
  // corr = IFFT(FFT(rec) * conj(FFT(chirp))), analytic: keep only positive
  // frequencies (doubled), zero the negative half before the inverse.
  for (let i = 0; i < size; i++) {
    const scale = (i === 0 || i === size / 2) ? 1 : (i < size / 2 ? 2 : 0);
    const r = (re[i] * hRe[i] + im[i] * hIm[i]) * scale;
    const x = (im[i] * hRe[i] - re[i] * hIm[i]) * scale;
    re[i] = r;
    im[i] = x;
  }
  fft(re, im, true);
  const env = new Float32Array(recording.length);
  for (let i = 0; i < env.length; i++) {
    env[i] = Math.hypot(re[i], im[i]);
  }
  return env;
}

// --- Arrival detection ---

// Expected chirp-start offsets (in phone samples, relative to chirp 0) for
// each slot of the schedule.
function slotOffsets(schedule, nSlots, sampleRate) {
  const perSample = sampleRate / schedule.sampleRate;
  const offsets = new Array(nSlots);
  for (let k = 0; k < nSlots; k++) {
    offsets[k] = Math.round(k * schedule.spacingSamples * perSample);
  }
  return offsets;
}

// Half-width, in samples, of the main lobe around an envelope peak: walk out
// either side until the envelope falls to LOBE_EDGE of the peak. Pure noise
// collapses to a sample or two, so an undetected slot keeps the minimum
// guard and scores as low as it did before.
function mainLobeHalfWidth(env, peak, lo, hi) {
  const edge = env[peak] * LOBE_EDGE;
  let left = peak;
  while (left > lo && env[left] > edge) left--;
  let right = peak;
  while (right < hi && env[right] > edge) right++;
  return Math.max(peak - left, right - peak);
}

// Find each slot's arrival time in the recording. Returns per-slot
// { sample, deviationS, confidence, detected } where deviationS is the
// arrival's offset from its scheduled position relative to the anchor -
// i.e. the quantity whose per-output differences are the flight-time
// differences. Throws if the recording is too short for the schedule.
export function findArrivals(recording, chirp, schedule, nSlots, sampleRate) {
  const env = correlationEnvelope(recording, chirp);
  const offsets = slotOffsets(schedule, nSlots, sampleRate);
  const lastOffset = offsets[nSlots - 1];
  const searchWin = Math.round(SEARCH_WINDOW_S * sampleRate);
  const spacing = schedule.spacingSamples * sampleRate / schedule.sampleRate;
  const bgWin = Math.max(searchWin, Math.round(Math.min(
    BACKGROUND_WINDOW_S * sampleRate,
    BACKGROUND_SPACING_FRACTION * spacing
  )));
  const minGuard = Math.round(MIN_GUARD_S * sampleRate);
  const maxGuard = Math.round(bgWin * MAX_GUARD_FRACTION);
  // Correlation is only meaningful while a whole chirp still fits after t.
  const envValid = env.length - chirp.length;

  const t0Max = env.length - lastOffset - chirp.length - searchWin;
  if (t0Max <= searchWin) {
    throw new Error('Recording is too short for the probe schedule');
  }

  // Global anchor: the chirp-0 start that maximizes the summed envelope at
  // every expected slot position - robust even when some outputs are silent.
  const score = new Float32Array(t0Max);
  for (let k = 0; k < nSlots; k++) {
    const off = offsets[k];
    for (let t = 0; t < t0Max; t++) {
      score[t] += env[t + off];
    }
  }
  let anchor = 0;
  for (let t = 1; t < t0Max; t++) {
    if (score[t] > score[anchor]) anchor = t;
  }

  const arrivals = [];
  for (let k = 0; k < nSlots; k++) {
    const center = anchor + offsets[k];
    const lo = Math.max(0, center - searchWin);
    const hi = Math.min(env.length - 1, center + searchWin);
    let peak = lo;
    for (let t = lo + 1; t <= hi; t++) {
      if (env[t] > env[peak]) peak = t;
    }

    // Sub-sample refinement: parabola through the envelope at the peak
    let refined = peak;
    if (peak > 0 && peak < env.length - 1) {
      const a = env[peak - 1];
      const b = env[peak];
      const c = env[peak + 1];
      const denom = a - 2 * b + c;
      if (denom < 0) refined = peak + 0.5 * (a - c) / denom;
    }

    // Confidence: peak against the RMS background around it, excluding the
    // peak's own main lobe - measured, not assumed, so a low-passed output's
    // broad lobe does not end up counted as its own background noise.
    const bgLo = Math.max(0, center - bgWin);
    const bgHi = Math.min(envValid, center + bgWin);
    const guard = Math.min(
      maxGuard,
      Math.max(minGuard, LOBE_GUARD * mainLobeHalfWidth(env, peak, bgLo, bgHi))
    );
    let sum = 0;
    let count = 0;
    for (let t = bgLo; t <= bgHi; t++) {
      if (Math.abs(t - peak) <= guard) continue;
      sum += env[t] * env[t];
      count++;
    }
    const background = count > 0 ? Math.sqrt(sum / count) : 0;
    const confidence = background > 0 ? env[peak] / background : 0;

    arrivals.push({
      sample: refined,
      deviationS: (refined - center) / sampleRate,
      confidence,
      detected: confidence >= MIN_CONFIDENCE,
    });
  }
  return arrivals;
}

// --- Delay computation ---

// Turn per-slot arrival deviations into per-output delay settings. The
// probe order is each output once ascending then the list reversed, so
// every output has two slots; averaging them cancels linear phone-clock
// drift (slot k and slot 2N-1-k always straddle the same midpoint time).
// The probe ran with the CURRENT user delays active, so corrections are
// incremental: align everything to the acoustically latest output, then
// re-normalize so the smallest delay is 0.
export function computeDelays(arrivals, order, currentDelaysUs, maxDelayUs) {
  const channels = new Map();
  order.forEach((output, slot) => {
    if (!channels.has(output)) {
      channels.set(output, { output, slots: [] });
    }
    channels.get(output).slots.push(arrivals[slot]);
  });

  const results = [];
  for (const ch of channels.values()) {
    const detected = ch.slots.filter((s) => s.detected);
    const measured = detected.length > 0;
    const offsetUs = measured
      ? detected.reduce((sum, s) => sum + s.deviationS, 0) / detected.length * 1e6
      : null;
    results.push({
      output: ch.output,
      measured,
      offsetUs,
      // Spread between the forward and reverse passes - a sanity signal
      // (echoes, noise, movement) the UI can surface.
      consistencyUs: detected.length === 2
        ? Math.abs(detected[0].deviationS - detected[1].deviationS) * 1e6
        : null,
      confidence: Math.min(...ch.slots.map((s) => s.confidence)),
      newDelayUs: null,
      clamped: false,
    });
  }

  const measured = results.filter((r) => r.measured);
  if (measured.length >= 2) {
    // Arrival time already includes each output's current delay, so the
    // residual misalignment is what remains to correct.
    const latest = Math.max(...measured.map((r) => r.offsetUs));
    let raw = measured.map((r) => ({
      r,
      delay: (currentDelaysUs[r.output] || 0) + (latest - r.offsetUs),
    }));
    const minDelay = Math.min(...raw.map((x) => x.delay));
    for (const x of raw) {
      let d = Math.round(x.delay - minDelay);
      if (d > maxDelayUs) {
        d = maxDelayUs;
        x.r.clamped = true;
      }
      x.r.newDelayUs = d;
    }
  }

  results.sort((a, b) => a.output - b.output);
  return results;
}

// --- Full pipeline ---

// recording: Float32Array from the mic worklet; sampleRate: the
// AudioContext rate; schedule/order: the /probe/delay/start response;
// currentDelaysUs: array indexed by output channel; maxDelayUs: device cap.
export function analyzeRecording(recording, sampleRate, schedule, currentDelaysUs, maxDelayUs) {
  const order = schedule.order;
  const chirp = generateChirp(sampleRate, schedule);
  const arrivals = findArrivals(recording, chirp, schedule, order.length, sampleRate);
  const channels = computeDelays(arrivals, order, currentDelaysUs, maxDelayUs);

  const measured = channels.filter((c) => c.measured);
  const spreadUs = measured.length >= 2
    ? Math.max(...measured.map((c) => c.offsetUs)) - Math.min(...measured.map((c) => c.offsetUs))
    : 0;
  return { channels, spreadUs };
}

// Path-difference equivalent of a time offset, for display.
export function usToCm(us) {
  return us * 1e-6 * SPEED_OF_SOUND_M_PER_S * 100;
}

// Total recording time the wizard should capture, with margin for network
// latency before the probe actually starts.
export function recordingDurationS(schedule, nSlots) {
  const samples = schedule.preRollSamples
    + (nSlots - 1) * schedule.spacingSamples
    + schedule.chirpSamples
    + schedule.tailSamples;
  return samples / schedule.sampleRate + 3;
}
