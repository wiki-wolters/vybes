/*
 * Shared RTA (real-time analyzer) math: band definitions, frame decoding,
 * FFT-to-band aggregation for the microphone path, and mic calibration
 * file parsing.
 */

// The standard 31 1/3-octave band centers, 20Hz-20kHz. Must match
// RTA_BAND_CENTERS in Teensy/fir_filters/fir_filters.ino.
export const RTA_BAND_CENTERS = [
  20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500,
  630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000,
  10000, 12500, 16000, 20000
];

export const RTA_NUM_BANDS = RTA_BAND_CENTERS.length;

// 1/3-octave band edges: center * 10^(+/-0.05)
const EDGE_LO = 0.89125;
const EDGE_HI = 1.12202;

// Decode a device RTA frame: two hex chars per band, value = (dB + 100) * 2.
// Returns per-band dB, or null if the frame is malformed.
export function decodeRtaFrame(hex) {
  if (typeof hex !== 'string' || hex.length < RTA_NUM_BANDS * 2) return null;
  const out = new Float32Array(RTA_NUM_BANDS);
  for (let i = 0; i < RTA_NUM_BANDS; i++) {
    const v = parseInt(hex.substr(i * 2, 2), 16);
    if (Number.isNaN(v)) return null;
    out[i] = v / 2 - 100;
  }
  return out;
}

// Aggregate AnalyserNode.getFloatFrequencyData output (dB per linear FFT
// bin) into the 31 bands. Edge bins contribute proportionally to their
// overlap with the band - the same scheme the Teensy uses, so the two
// spectra are directly comparable. Returns per-band dB.
export function bandsFromFFT(freqData, binWidth) {
  const out = new Float32Array(RTA_NUM_BANDS);
  for (let b = 0; b < RTA_NUM_BANDS; b++) {
    const lo = RTA_BAND_CENTERS[b] * EDGE_LO;
    const hi = RTA_BAND_CENTERS[b] * EDGE_HI;
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

// Parse a mic calibration file (REW-style text: "frequency gain" per line,
// whitespace or comma separated; lines starting with * # ; or " are
// comments). Returns the correction in dB at each band center - subtract
// it from the measured mic level - or null if nothing parseable.
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
  return RTA_BAND_CENTERS.map((fc) => interpolateLogFreq(points, fc));
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

// Median of (a[i] - b[i]) over the bands whose center lies in [loHz, hiHz].
// Used to auto-align the mic trace level with the source trace.
export function medianOffset(a, b, loHz = 200, hiHz = 5000) {
  const diffs = [];
  for (let i = 0; i < RTA_NUM_BANDS; i++) {
    const fc = RTA_BAND_CENTERS[i];
    if (fc >= loHz && fc <= hiHz && a[i] > -95 && b[i] > -95) {
      diffs.push(a[i] - b[i]);
    }
  }
  if (diffs.length === 0) return 0;
  diffs.sort((x, y) => x - y);
  const mid = Math.floor(diffs.length / 2);
  return diffs.length % 2 ? diffs[mid] : (diffs[mid - 1] + diffs[mid]) / 2;
}
