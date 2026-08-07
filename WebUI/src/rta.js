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

export const BUILTIN_CAL_PRESETS = [
  {
    id: 'smartphone-hpf',
    name: 'Generic smartphone (approx.)',
    points: hpfDeviationPoints(55),
  },
];

// Median of (a[i] - b[i]) over the bands whose center lies in [loHz, hiHz].
// Used to auto-align the mic trace level with the source trace. a, b and
// centers must share one grid.
export function medianOffset(a, b, centers, loHz = 200, hiHz = 5000) {
  const diffs = [];
  for (let i = 0; i < centers.length; i++) {
    if (centers[i] >= loHz && centers[i] <= hiHz && a[i] > -95 && b[i] > -95) {
      diffs.push(a[i] - b[i]);
    }
  }
  if (diffs.length === 0) return 0;
  diffs.sort((x, y) => x - y);
  const mid = Math.floor(diffs.length / 2);
  return diffs.length % 2 ? diffs[mid] : (diffs[mid - 1] + diffs[mid]) / 2;
}
