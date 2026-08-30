/*
 * Synthetic acoustic systems for the auto-FIR acceptance tests.
 *
 * This is the verification harness for slice B (docs/AUTO_FIR_CONTRACTS.md):
 * it builds known systems - delay, band limits, resonances, allpass excess
 * phase, a discrete reflection - simulates capturing a stimulus through
 * them (clock drift, noise), and measures results with its own independent
 * FFT/DFT machinery. Slice B implements against this file and does not
 * modify it; a genuinely wrong threshold is a contract change.
 *
 * Everything is deliberately self-contained - no imports from src/ - so a
 * bug in the code under test can never hide inside its own test harness.
 */

// --- deterministic rng (mulberry32) ---

export function makeRng(seed) {
  let a = seed >>> 0;
  return () => {
    a |= 0; a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

export function nextPow2(n) {
  let p = 1;
  while (p < n) p <<= 1;
  return p;
}

// --- FFT: iterative radix-2, complex, in-place, float64 ---

export function fft(re, im, inverse) {
  const n = re.length;
  if (n < 2 || (n & (n - 1)) !== 0) throw new Error('fft size must be a power of 2');
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      const tr = re[i]; re[i] = re[j]; re[j] = tr;
      const ti = im[i]; im[i] = im[j]; im[j] = ti;
    }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = ((inverse ? 2 : -2) * Math.PI) / len;
    const wr = Math.cos(ang);
    const wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cwr = 1;
      let cwi = 0;
      for (let k = 0; k < len / 2; k++) {
        const ur = re[i + k];
        const ui = im[i + k];
        const vr = re[i + k + len / 2] * cwr - im[i + k + len / 2] * cwi;
        const vi = re[i + k + len / 2] * cwi + im[i + k + len / 2] * cwr;
        re[i + k] = ur + vr;
        im[i + k] = ui + vi;
        re[i + k + len / 2] = ur - vr;
        im[i + k + len / 2] = ui - vi;
        const nwr = cwr * wr - cwi * wi;
        cwi = cwr * wi + cwi * wr;
        cwr = nwr;
      }
    }
  }
  if (inverse) {
    for (let i = 0; i < n; i++) { re[i] /= n; im[i] /= n; }
  }
}

// --- component responses (analog prototypes; a simulator doesn't need
//     bilinear warping, just plausible magnitude/phase shapes) ---

// 2nd-order Butterworth band edges, magnitude only.
function bandMag(f, band) {
  if (f <= 0) return 0;
  let m = 1;
  if (band && band.lo > 0) {
    const w = (f / band.lo) ** 2;
    m *= w / Math.sqrt(1 + w * w);
  }
  if (band && band.hi > 0) {
    const w = (f / band.hi) ** 2;
    m *= 1 / Math.sqrt(1 + w * w);
  }
  return m;
}

// Analog peaking EQ magnitude; |H| at freq is exactly 10^(gainDb/20).
function peakingMag(f, { freq, q, gainDb }) {
  const A = Math.pow(10, gainDb / 40);
  const w = f / freq;
  const d = (1 - w * w) ** 2;
  const n2 = d + (A * w / q) ** 2;
  const d2 = d + (w / (A * q)) ** 2;
  return Math.sqrt(n2 / d2);
}

// 2nd-order analog allpass phase (0 at DC, -2*pi at f >> freq).
export function allpassPhase(f, { freq, q }) {
  const w = 2 * Math.PI * f;
  const w0 = 2 * Math.PI * freq;
  return -2 * Math.atan2((w * w0) / q, w0 * w0 - w * w);
}

// Minimum-phase complex spectrum for a magnitude sampled on bins 0..n/2,
// via the real cepstrum: c = IFFT(log|H|), fold to causal, Hmin = exp(FFT(c)).
function minimumPhaseSpectrum(mag, n) {
  const floor = 1e-7; // ~ -140 dB, keeps log finite where the band is closed
  const re = new Float64Array(n);
  const im = new Float64Array(n);
  for (let k = 0; k <= n / 2; k++) {
    const v = Math.log(Math.max(mag[k], floor));
    re[k] = v;
    if (k > 0 && k < n / 2) re[n - k] = v;
  }
  fft(re, im, true); // real cepstrum
  for (let q = 1; q < n / 2; q++) {
    re[q] *= 2;
    re[n - q] = 0;
  }
  im.fill(0);
  fft(re, im, false); // log Hmin
  const outRe = new Float64Array(n / 2 + 1);
  const outIm = new Float64Array(n / 2 + 1);
  for (let k = 0; k <= n / 2; k++) {
    const e = Math.exp(re[k]);
    outRe[k] = e * Math.cos(im[k]);
    outIm[k] = e * Math.sin(im[k]);
  }
  return { re: outRe, im: outIm };
}

/**
 * Build a synthetic system impulse response.
 *
 * spec: {
 *   length      IR length, power of 2 (default 16384)
 *   delayS      acoustic flight time (default 0)
 *   gain        broadband linear gain (default 1)
 *   polarity    +1 | -1 (default +1)
 *   band        { lo, hi } 2nd-order Butterworth edges (default 20/20000)
 *   resonances  [{ freq, q, gainDb }] peaking errors (default [])
 *   allpasses   [{ freq, q }] excess-phase rotations (default [])
 *   reflection  { delayS, gain } one discrete echo (default null)
 * }
 * The magnitude chain is rendered minimum-phase (like a real driver), then
 * delay, allpasses and the reflection multiply in as excess terms.
 */
export function makeSystemIr(sampleRate, spec = {}) {
  const {
    length = 16384,
    delayS = 0,
    gain = 1,
    polarity = 1,
    band = { lo: 20, hi: 20000 },
    resonances = [],
    allpasses = [],
    reflection = null,
  } = spec;
  const n = nextPow2(length);
  const mag = new Float64Array(n / 2 + 1);
  for (let k = 0; k <= n / 2; k++) {
    const f = (k * sampleRate) / n;
    let m = gain * bandMag(f, band);
    for (const r of resonances) m *= peakingMag(f, r);
    mag[k] = m;
  }
  const mp = minimumPhaseSpectrum(mag, n);
  const re = new Float64Array(n);
  const im = new Float64Array(n);
  for (let k = 0; k <= n / 2; k++) {
    const f = (k * sampleRate) / n;
    const w = 2 * Math.PI * f;
    let ph = -w * delayS;
    for (const ap of allpasses) ph += allpassPhase(f, ap);
    const c = Math.cos(ph);
    const s = Math.sin(ph);
    let r = mp.re[k] * c - mp.im[k] * s;
    let i2 = mp.re[k] * s + mp.im[k] * c;
    if (reflection) {
      const rc = 1 + reflection.gain * Math.cos(w * reflection.delayS);
      const rs = -reflection.gain * Math.sin(w * reflection.delayS);
      const nr = r * rc - i2 * rs;
      i2 = r * rs + i2 * rc;
      r = nr;
    }
    re[k] = polarity * r;
    im[k] = polarity * i2;
    if (k > 0 && k < n / 2) {
      re[n - k] = re[k];
      im[n - k] = -im[k];
    }
  }
  im[0] = 0;
  im[n / 2] = 0;
  fft(re, im, true);
  const ir = new Float32Array(n);
  for (let k = 0; k < n; k++) ir[k] = re[k];
  return ir;
}

// --- measurement machinery ---

/** Linear convolution via FFT; returns a.length + b.length - 1 samples. */
export function convolve(a, b) {
  const outLen = a.length + b.length - 1;
  const n = nextPow2(outLen);
  const are = new Float64Array(n);
  const aim = new Float64Array(n);
  const bre = new Float64Array(n);
  const bim = new Float64Array(n);
  are.set(a);
  bre.set(b);
  fft(are, aim, false);
  fft(bre, bim, false);
  for (let k = 0; k < n; k++) {
    const r = are[k] * bre[k] - aim[k] * bim[k];
    aim[k] = are[k] * bim[k] + aim[k] * bre[k];
    are[k] = r;
  }
  fft(are, aim, true);
  const out = new Float32Array(outLen);
  for (let k = 0; k < outLen; k++) out[k] = are[k];
  return out;
}

/** Direct DFT at arbitrary frequencies. Returns dB magnitude and phase. */
export function freqResponse(ir, sampleRate, freqs) {
  const magDb = new Float64Array(freqs.length);
  const phaseRad = new Float64Array(freqs.length);
  for (let i = 0; i < freqs.length; i++) {
    const w = (2 * Math.PI * freqs[i]) / sampleRate;
    let re = 0;
    let im = 0;
    for (let nn = 0; nn < ir.length; nn++) {
      re += ir[nn] * Math.cos(w * nn);
      im -= ir[nn] * Math.sin(w * nn);
    }
    magDb[i] = 20 * Math.log10(Math.max(Math.hypot(re, im), 1e-12));
    phaseRad[i] = Math.atan2(im, re);
  }
  return { magDb, phaseRad };
}

/** Log-spaced frequency grid, inclusive of fLo. */
export function logSpace(fLo, fHi, pointsPerOctave) {
  const out = [];
  const step = Math.pow(2, 1 / pointsPerOctave);
  for (let f = fLo; f <= fHi * 1.0001; f *= step) out.push(f);
  return Float64Array.from(out);
}

/**
 * Resample by a length factor (Catmull-Rom cubic): output length is
 * round(length * factor), sampling the input at n / factor. Reproduces
 * polynomials up to cubic exactly, so linear ramps survive untouched.
 */
export function stretch(signal, factor) {
  const outLen = Math.round(signal.length * factor);
  const out = new Float32Array(outLen);
  const at = (i) => (i >= 0 && i < signal.length ? signal[i] : 0);
  for (let n = 0; n < outLen; n++) {
    const t = n / factor;
    const i = Math.floor(t);
    const fr = t - i;
    const y0 = at(i - 1);
    const y1 = at(i);
    const y2 = at(i + 1);
    const y3 = at(i + 2);
    out[n] =
      y1 +
      0.5 *
        fr *
        (y2 - y0 + fr * (2 * y0 - 5 * y1 + 4 * y2 - y3 + fr * (3 * (y1 - y2) + y3 - y0)));
  }
  return out;
}

/**
 * Simulate capturing `stimulus` played through `systemIr`:
 * pre-roll silence + convolution, stretched by the capture clock's drift,
 * plus uniform noise at the given RMS level. Deterministic per seed.
 */
export function simulateCapture(systemIr, stimulus, opts = {}) {
  const {
    sampleRate = 44100,
    driftPpm = 0,
    noiseDbFs = -90,
    preRollS = 0,
    seed = 1,
  } = opts;
  const wet = convolve(stimulus, systemIr);
  const pre = Math.round(preRollS * sampleRate);
  let sig = new Float32Array(pre + wet.length);
  sig.set(wet, pre);
  if (driftPpm !== 0) sig = stretch(sig, 1 + driftPpm * 1e-6);
  const amp = Math.pow(10, noiseDbFs / 20) * Math.sqrt(3); // uniform -> RMS
  const rng = makeRng(seed);
  for (let n = 0; n < sig.length; n++) sig[n] += amp * (2 * rng() - 1);
  return sig;
}

/**
 * Excess phase of an IR at the given frequencies: measured phase minus the
 * minimum phase implied by its own magnitude, with the best-fit linear
 * component (bulk delay) removed. Radians.
 */
export function excessPhaseOf(ir, sampleRate, freqs) {
  const n = nextPow2(ir.length) * 2;
  const re = new Float64Array(n);
  const im = new Float64Array(n);
  re.set(ir);
  fft(re, im, false);
  const mag = new Float64Array(n / 2 + 1);
  for (let k = 0; k <= n / 2; k++) mag[k] = Math.hypot(re[k], im[k]);
  const mp = minimumPhaseSpectrum(mag, n);
  // Wrapped per-bin excess, then unwrap across the band of interest.
  const kLo = Math.max(1, Math.floor((freqs[0] * n) / sampleRate));
  const kHi = Math.min(n / 2, Math.ceil((freqs[freqs.length - 1] * n) / sampleRate));
  const exc = new Float64Array(kHi - kLo + 1);
  let prev = 0;
  let offset = 0;
  for (let k = kLo; k <= kHi; k++) {
    const ph = Math.atan2(im[k], re[k]) - Math.atan2(mp.im[k], mp.re[k]);
    let w = ph - prev;
    while (w > Math.PI) { w -= 2 * Math.PI; offset -= 2 * Math.PI; }
    while (w < -Math.PI) { w += 2 * Math.PI; offset += 2 * Math.PI; }
    prev = ph;
    exc[k - kLo] = ph + offset;
  }
  // Least-squares linear detrend against bin index (proportional to omega).
  let sx = 0;
  let sy = 0;
  let sxx = 0;
  let sxy = 0;
  const m = exc.length;
  for (let i = 0; i < m; i++) {
    sx += i; sy += exc[i]; sxx += i * i; sxy += i * exc[i];
  }
  const slope = (m * sxy - sx * sy) / (m * sxx - sx * sx);
  const icept = (sy - slope * sx) / m;
  for (let i = 0; i < m; i++) exc[i] -= icept + slope * i;
  // Sample at the requested frequencies (linear interp over bins).
  const out = new Float64Array(freqs.length);
  for (let i = 0; i < freqs.length; i++) {
    const kf = (freqs[i] * n) / sampleRate;
    const k0 = Math.min(Math.max(Math.floor(kf), kLo), kHi - 1);
    const fr = Math.min(Math.max(kf - k0, 0), 1);
    out[i] = exc[k0 - kLo] * (1 - fr) + exc[k0 - kLo + 1] * fr;
  }
  return out;
}

/** RMS of an array. */
export function rms(arr) {
  let s = 0;
  for (let i = 0; i < arr.length; i++) s += arr[i] * arr[i];
  return Math.sqrt(s / arr.length);
}

/** Index of the largest absolute value. */
export function argmaxAbs(arr) {
  let best = 0;
  let bv = -1;
  for (let i = 0; i < arr.length; i++) {
    const v = Math.abs(arr[i]);
    if (v > bv) { bv = v; best = i; }
  }
  return best;
}

/**
 * Test-side measurement extraction, independent of slice B's gatedResponse:
 * magnitude + excess phase of an IR on a log grid. For synthetic systems
 * with no reflection this is exact, so acceptance tests can hand it straight
 * to designKernel.
 */
export function measurementOf(ir, sampleRate, { fLo, fHi, pointsPerOctave = 24 }) {
  const freqs = logSpace(fLo, fHi, pointsPerOctave);
  const { magDb } = freqResponse(ir, sampleRate, freqs);
  const excessPhaseRad = excessPhaseOf(ir, sampleRate, freqs);
  return { freqs, magDb, excessPhaseRad };
}
