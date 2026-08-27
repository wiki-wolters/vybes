/*
 * Shared RTA (real-time analyzer) math: band grids, frame decoding,
 * FFT-to-band aggregation for the microphone path, grid-to-grid
 * aggregation, and mic calibration file parsing.
 *
 * Bands are 1/N-octave in the base-10 sense (a 1/3-octave step is
 * 10^(1/10)), centers 10^(k/perDecade) covering 20Hz-20kHz. The device
 * (Teensy/fir_filters.ino) uses the same definition; the UI infers a
 * frame's resolution from its band count.
 */

// Supported resolutions: bands-per-octave -> total band count 20Hz-20kHz
export const RTA_RESOLUTIONS = { 3: 31, 6: 61, 12: 121 };

const GRID_CACHE = new Map();

// Band grid for a 1/N-octave resolution (N = 3, 6 or 12).
// centers[i] = 10^((kLo + i) / perDecade); edges at center * 10^(±1/(2*perDecade)).
export function makeBandGrid(bandsPerOctave) {
  const cached = GRID_CACHE.get(bandsPerOctave);
  if (cached) return cached;
  const perDecade = (10 * bandsPerOctave) / 3; // 10, 20 or 40
  const kLo = Math.round(perDecade * Math.log10(20));
  const kHi = Math.round(perDecade * Math.log10(20000));
  const centers = [];
  for (let k = kLo; k <= kHi; k++) centers.push(Math.pow(10, k / perDecade));
  const grid = {
    bandsPerOctave,
    perDecade,
    kLo,
    centers,
    edgeLo: Math.pow(10, -1 / (2 * perDecade)),
    edgeHi: Math.pow(10, 1 / (2 * perDecade)),
  };
  GRID_CACHE.set(bandsPerOctave, grid);
  return grid;
}

// Frame band count -> bands per octave. Only these counts are valid frames.
const BPO_BY_BAND_COUNT = { 31: 3, 61: 6, 121: 12 };

// Decode a device RTA frame: two hex chars per band, value = (dB + 100) * 2.
// The band count (and thus the resolution) is inferred from the length.
// Returns { values, grid }, or null if the frame is malformed.
export function decodeRtaFrame(hex) {
  if (typeof hex !== 'string') return null;
  const bandCount = hex.length / 2;
  const bpo = BPO_BY_BAND_COUNT[bandCount];
  if (!bpo) return null;
  const values = new Float32Array(bandCount);
  for (let i = 0; i < bandCount; i++) {
    const v = parseInt(hex.substr(i * 2, 2), 16);
    if (Number.isNaN(v)) return null;
    values[i] = v / 2 - 100;
  }
  return { values, grid: makeBandGrid(bpo) };
}

// Aggregate AnalyserNode.getFloatFrequencyData output (dB per linear FFT
// bin) into the grid's bands. Edge bins contribute proportionally to their
// overlap with the band - the same scheme the Teensy uses, so the two
// spectra are directly comparable. Returns per-band dB.
export function bandsFromFFT(freqData, binWidth, grid) {
  const n = grid.centers.length;
  const out = new Float32Array(n);
  for (let b = 0; b < n; b++) {
    const lo = grid.centers[b] * grid.edgeLo;
    const hi = grid.centers[b] * grid.edgeHi;
    const first = Math.max(1, Math.round(lo / binWidth));
    const last = Math.min(freqData.length - 1, Math.round(hi / binWidth));
    let power = 0;
    for (let i = first; i <= last; i++) {
      const overlap = Math.min(hi, (i + 0.5) * binWidth) - Math.max(lo, (i - 0.5) * binWidth);
      if (overlap <= 0 || !isFinite(freqData[i])) continue;
      power += Math.pow(10, freqData[i] / 10) * (overlap / binWidth);
    }
    out[b] = power > 1e-12 ? 10 * Math.log10(power) : -120;
  }
  return out;
}

// Re-bin per-band dB values from a fine grid onto a coarser one. Band powers
// add; a fine band straddling a coarse band edge contributes to each side in
// proportion to its (log-frequency) overlap. Values at the -120 floor are
// treated as silence. Returns per-band dB on the target grid.
export function aggregateBands(valuesDb, fromGrid, toGrid) {
  if (fromGrid === toGrid) return valuesDb;
  const halfFrom = 1 / (2 * fromGrid.perDecade);
  const halfTo = 1 / (2 * toGrid.perDecade);
  const out = new Float32Array(toGrid.centers.length);
  for (let b = 0; b < toGrid.centers.length; b++) {
    const cTo = Math.log10(toGrid.centers[b]);
    const lo = cTo - halfTo;
    const hi = cTo + halfTo;
    let power = 0;
    for (let i = 0; i < fromGrid.centers.length; i++) {
      const c = (fromGrid.kLo + i) / fromGrid.perDecade;
      const overlap = Math.min(hi, c + halfFrom) - Math.max(lo, c - halfFrom);
      if (overlap <= 0 || valuesDb[i] <= -119) continue;
      power += Math.pow(10, valuesDb[i] / 10) * (overlap / (2 * halfFrom));
    }
    out[b] = power > 1e-12 ? 10 * Math.log10(power) : -120;
  }
  return out;
}

// Parse a mic calibration file (REW-style text: "frequency gain" per line,
// whitespace or comma separated; lines starting with * # ; or " are
// comments). Returns sorted [frequency, gain] points, or null if nothing
// parseable. Interpolate onto a grid with calCurveForGrid.
export function parseCalibrationFile(text) {
  const points = [];
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || /^[*#;"]/.test(line)) continue;
    const parts = line.split(/[\s,]+/).map(Number);
    if (parts.length >= 2 && isFinite(parts[0]) && isFinite(parts[1]) && parts[0] > 0) {
      points.push([parts[0], parts[1]]);
    }
  }
  if (points.length < 2) return null;
  points.sort((a, b) => a[0] - b[0]);
  return points;
}

// Correction in dB at each of the grid's band centers - subtract it from
// the measured mic level.
export function calCurveForGrid(points, grid) {
  return grid.centers.map((fc) => interpolateLogFreq(points, fc));
}

// Linear interpolation in log-frequency; clamps outside the file's range.
function interpolateLogFreq(points, freq) {
  if (freq <= points[0][0]) return points[0][1];
  const last = points[points.length - 1];
  if (freq >= last[0]) return last[1];
  for (let i = 1; i < points.length; i++) {
    if (freq <= points[i][0]) {
      const [f0, g0] = points[i - 1];
      const [f1, g1] = points[i];
      const t = (Math.log10(freq) - Math.log10(f0)) / (Math.log10(f1) - Math.log10(f0));
      return g0 + t * (g1 - g0);
    }
  }
  return last[1];
}

// --- Per-band peak hold ---
// The ballistic of a hardware meter: a band that rises takes the new value
// and is pinned there for holdMs, then falls at decayDbPerSec until
// something pushes it up again. The hold stage is what makes the layer worth
// drawing - on a steady signal whose frame-to-frame jitter is smaller than
// one frame's worth of decay, a hold-less peak just sits on the current
// level and tells you nothing.
export function makePeakHold(bandCount, initDb = -200) {
  return {
    values: new Float32Array(bandCount).fill(initDb),
    holdUntil: new Float64Array(bandCount),
  };
}

export function updatePeakHold(state, newDb, nowMs, dtMs, decayDbPerSec, holdMs) {
  const fall = (decayDbPerSec * dtMs) / 1000;
  const { values, holdUntil } = state;
  for (let i = 0; i < newDb.length; i++) {
    if (newDb[i] >= values[i]) {
      values[i] = newDb[i];
      holdUntil[i] = nowMs + holdMs;
    } else if (nowMs >= holdUntil[i]) {
      values[i] = Math.max(newDb[i], values[i] - fall);
    }
  }
  return values;
}

// Power-domain average of several per-band dB arrays (deviation snapshots
// from different mic positions). Per band, the mean is taken over the
// arrays that have a finite value there; NaN when none does. All arrays
// must share one grid.
export function averageDbArrays(arrays) {
  if (!arrays.length) return null;
  const n = arrays[0].length;
  const out = new Array(n);
  for (let i = 0; i < n; i++) {
    let power = 0;
    let count = 0;
    for (const a of arrays) {
      if (Number.isFinite(a[i])) {
        power += Math.pow(10, a[i] / 10);
        count++;
      }
    }
    out[i] = count ? 10 * Math.log10(power / count) : NaN;
  }
  return out;
}

// --- Built-in mic profiles ---
// Approximate deviation of a smartphone mic captured in the browser: the
// MEMS capsule itself is nearly flat to 20 Hz, but the OS/browser capture
// chain applies a high-pass (roughly 2nd-order around 55 Hz) that no
// getUserMedia constraint can disable. The points are 20*log10|H| of that
// filter, applied like any imported cal file. "Approximate" because the
// corner varies by device and OS version.
function hpfDeviationPoints(cornerHz) {
  const freqs = [20, 25, 32, 40, 50, 63, 80, 100, 125, 160, 200, 315, 20000];
  return freqs.map((f) => {
    const r2 = (f / cornerHz) ** 2;
    const db = 20 * Math.log10(r2 / Math.sqrt(1 + r2 * r2));
    return [f, Math.round(db * 10) / 10];
  });
}

// Deviation of the iPhone 17 Pro's built-in microphone, digitised from Faber
// Acoustical's anechoic measurement (PCB 378B02 reference at 1mm; the
// pressure-corrected one of the two published traces - they only diverge
// above 5kHz). Flat within ~1.5 dB from 100Hz to 5kHz, so the correction
// that matters is all in the low end, reaching -13 dB at 20Hz.
//
// This is the acoustic path as captured in iOS measurement mode, so it does
// NOT include any further high-pass Safari's getUserMedia may apply on top.
// If it does apply one, this profile under-corrects the bottom octave - the
// two possibilities differ by ~14 dB at 25Hz. Switching between this and
// 'smartphone-hpf' on a known source tells you which chain you're on.
//
// Above 10kHz the points follow the measured port resonance, but treat them
// as indicative: it moves with orientation, and the auto-EQ's default range
// stops at 10kHz regardless.
const IPHONE_17_PRO_POINTS = [
  [20, -13.0], [22, -11.8], [25, -10.3], [28, -9.0], [31.5, -7.6], [35, -6.4],
  [40, -5.1], [45, -4.3], [50, -3.6], [56, -3.0], [63, -2.5], [71, -2.2],
  [80, -1.9], [90, -1.7], [100, -1.6], [125, -1.4], [160, -1.2], [200, -1.0],
  [250, -0.8], [315, -0.7], [400, -0.6], [500, -0.5], [630, -0.4], [800, -0.3],
  [1000, -0.2], [1600, -0.1], [2500, 0], [4000, 0], [5000, -0.1], [6300, -0.6],
  [8000, -1.7], [10000, -3.6], [11000, -4.6], [13000, 4.5], [15000, -1.0],
  [17500, 1.9], [19000, -13.0], [20000, -17.0],
];

export const BUILTIN_CAL_PRESETS = [
  {
    id: 'smartphone-hpf',
    name: 'Generic smartphone (approx.)',
    points: hpfDeviationPoints(55),
  },
  {
    id: 'iphone-17-pro',
    name: 'iPhone 17 Pro',
    points: IPHONE_17_PRO_POINTS,
  },
];

// --- Capture chain readback ---
// What the browser actually granted for a mic capture, taken from the
// track's getSettings(). We ask for raw audio - no echo cancellation, no
// noise suppression, no AGC - but a constraint is a request, not a promise:
// iOS can hand back a voice-processed track regardless, and a chain tuned
// for speech reshapes the 200Hz-5kHz band medianOffset pins its alignment
// to. Correct that and the EQ is correcting the microphone, not the room.
//
// Three states per processor, and the third one is the reason this exists:
// Safari commonly omits these keys rather than reporting them false, so
// 'unknown' has to stay visibly distinct from 'off'. Reading a missing key
// as "disabled" is precisely the false all-clear worth not giving.
export const CAPTURE_PROCESSORS = [
  ['echoCancellation', 'echo cancellation'],
  ['noiseSuppression', 'noise suppression'],
  ['autoGainControl', 'auto gain'],
];

export function describeCaptureSettings(settings) {
  if (!settings) return null;
  const processors = CAPTURE_PROCESSORS.map(([key, label]) => ({
    key,
    label,
    // Nullish, not just undefined: a browser that answers null has told us
    // nothing either, and that has to land in 'unknown' too.
    state: settings[key] == null ? 'unknown' : settings[key] ? 'on' : 'off',
  }));
  const rate = settings.sampleRate;
  const channels = settings.channelCount;
  return {
    processors,
    // Something is demonstrably processing the capture.
    processed: processors.some((p) => p.state === 'on'),
    // Nothing was reported either way - the readback proves nothing here.
    unreported: processors.every((p) => p.state === 'unknown'),
    sampleRate: Number.isFinite(rate)
      ? `${(rate / 1000).toFixed(rate % 1000 ? 1 : 0)} kHz`
      : null,
    channels:
      channels === 1 ? 'mono' : channels === 2 ? 'stereo'
        : Number.isFinite(channels) ? `${channels} ch` : null,
  };
}

// Median of (a[i] - b[i]) over the bands whose center lies in [loHz, hiHz].
// Used to auto-align the mic trace level (a) with the source trace (b). a, b
// and centers must share one grid. When a floor array is supplied, bands
// where the mic sits within floorMarginDb of its noise floor are dropped, so
// bands the scoped output can't reproduce (mic at floor) never bias the
// offset - the alignment happens only where there's real signal to align.
export function medianOffset(a, b, centers, loHz = 200, hiHz = 5000, floor = null, floorMarginDb = 0) {
  const diffs = [];
  for (let i = 0; i < centers.length; i++) {
    if (
      centers[i] >= loHz && centers[i] <= hiHz && a[i] > -95 && b[i] > -95 &&
      (!floor || a[i] >= floor[i] + floorMarginDb)
    ) {
      diffs.push(a[i] - b[i]);
    }
  }
  if (diffs.length === 0) return 0;
  diffs.sort((x, y) => x - y);
  const mid = Math.floor(diffs.length / 2);
  return diffs.length % 2 ? diffs[mid] : (diffs[mid - 1] + diffs[mid]) / 2;
}

// --- Level-alignment window ---
// The band the mic trace is lined up with the source over before the two are
// differenced. It has to fall where the scoped output actually makes sound
// AND where the correction is aimed, or the offset gets computed from bands
// nobody is measuring: a soloed sub aligned on 200-5000 Hz is aligned on its
// own leakage, which tilts its entire deviation up by ~20 dB.
//
// Seeded with the correction band [loHz, hiHz] - narrowing that band is the
// user saying which range they care about, and the alignment has to follow it
// or the deviation inside the band is an offset from outside it. Then pulled
// in to the output's passband, a half octave clear of each crossover corner
// so the rolloff skirts can't drag the offset. Where what's left still
// reaches the midrange, align there instead: a woofer aligns from 200 Hz up
// rather than through its modal region, while a sub sits entirely below it
// and keeps its own band.
export const ALIGN_SKIRT = Math.SQRT2; // half octave
const MIN_ALIGN_RATIO = 1.26; // 1/3 octave - the narrowest window worth a median
const MID_LO_HZ = 200;
const MID_HI_HZ = 5000;

export function alignmentWindow({ loHz = 20, hiHz = 20000, hpHz = null, lpHz = null } = {}) {
  let lo = Math.min(loHz, hiHz);
  let hi = Math.max(loHz, hiHz);
  if (hpHz || lpHz) {
    const inLo = Math.max(lo, (hpHz ?? 0) * ALIGN_SKIRT);
    const inHi = Math.min(hi, lpHz ? lpHz / ALIGN_SKIRT : Infinity);
    // Only take the narrowed window when enough of it survives to take a
    // median over - a passband narrower than its own skirts (a sub crossed
    // just above its corner) leaves the requested band the best estimate
    // available.
    if (inHi / inLo >= MIN_ALIGN_RATIO) {
      lo = inLo;
      hi = inHi;
    }
  }
  const midLo = Math.max(lo, MID_LO_HZ);
  const midHi = Math.min(hi, MID_HI_HZ);
  if (midHi / midLo >= MIN_ALIGN_RATIO) return { loHz: midLo, hiHz: midHi };
  return { loHz: lo, hiHz: hi };
}
