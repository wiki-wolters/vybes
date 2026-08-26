/*
 * Geometry for the analyzer's deviation charts.
 *
 * Two charts now draw the same measurement: the full one on the analyzer page
 * and the stripped-back one inside the position-capture modal. They have
 * different widths and heights but must read as the same picture - a band has
 * to sit at the same place on the frequency axis and a +6 dB bar has to look
 * like +6 dB in both - so the scale lives here rather than in either view.
 *
 * Everything is pure and takes its geometry explicitly, which is also what
 * makes it testable without mounting a chart.
 */

// Log-frequency x axis with a fixed span (the outer edges of the 1/3-octave
// 20Hz-20kHz range), so traces of different resolutions share the chart and
// the axis doesn't shift when the resolution changes.
export const LOG_X_LO = 1.25; // log10(20) - half a 1/3-octave band
export const LOG_X_HI = 4.35; // log10(20000) + half a 1/3-octave band

// Deviation past ±20 dB is off the chart in both senses: the trace is clamped
// so one pathological band cannot flatten the rest of the picture.
export const DELTA_RANGE_DB = 20;

// Bar fills are bound per-bar, so they can't come from a stylesheet rule.
// Kept in sync with the theme amber (--vybes-accent) by hand.
export const DELTA_BOOST_COLOR = '#f5c04e';
export const DELTA_LOSS_COLOR = '#38bdf8';

export const clampDelta = (d) =>
  Math.max(-DELTA_RANGE_DB, Math.min(DELTA_RANGE_DB, d));

export const logX = (freq, width, padLeft) =>
  padLeft + ((Math.log10(freq) - LOG_X_LO) / (LOG_X_HI - LOG_X_LO)) * (width - padLeft);

export const freqAtLogX = (x, width, padLeft) =>
  Math.pow(10, LOG_X_LO + ((x - padLeft) / (width - padLeft)) * (LOG_X_HI - LOG_X_LO));

/** Pixel width of one band (bands are equal-width in log frequency) */
export const bandPixelWidth = (grid, width, padLeft) =>
  (width - padLeft) / ((LOG_X_HI - LOG_X_LO) * grid.perDecade);

/** Zero-deviation line, and the dB -> y mapping around it. */
export const deltaZeroY = (height) => height / 2;

// The 8px inset keeps a clamped ±20 dB bar from touching the chart edge,
// where it would be indistinguishable from one running off the top.
export const deltaDbToY = (db, height) =>
  deltaZeroY(height) - (db / DELTA_RANGE_DB) * (height / 2 - 8);

/**
 * Deviation bars: one per band with a finite value, grown from the zero line.
 *
 * Bands within ±2 dB are drawn faint. That band is inside the measurement's
 * own uncertainty from a single mic position, so showing it at full strength
 * invites correcting noise.
 */
export function deviationBars(values, grid, { width, padLeft, height, barFraction = 0.66 }) {
  if (!values) return [];
  const zero = deltaZeroY(height);
  const bw = bandPixelWidth(grid, width, padLeft) * barFraction;
  const bars = [];
  for (let i = 0; i < grid.centers.length; i++) {
    if (!Number.isFinite(values[i])) continue;
    const d = clampDelta(values[i]);
    bars.push({
      index: i,
      x: logX(grid.centers[i], width, padLeft) - bw / 2,
      y: deltaDbToY(Math.max(0, d), height),
      w: bw,
      h: Math.max(1, Math.abs(deltaDbToY(d, height) - zero)),
      color: d >= 0 ? DELTA_BOOST_COLOR : DELTA_LOSS_COLOR,
      opacity: Math.abs(d) < 2 ? 0.35 : 0.9,
    });
  }
  return bars;
}

/**
 * SVG path through a per-band deviation series.
 *
 * Gated bands (NaN - the source had no content there) are skipped rather than
 * bridged, so the line does not invent a response across a gap.
 */
export function deviationPath(values, grid, { width, padLeft, height }) {
  if (!values) return '';
  const seg = [];
  for (let i = 0; i < grid.centers.length; i++) {
    const d = values[i];
    if (!Number.isFinite(d)) continue;
    seg.push(
      `${logX(grid.centers[i], width, padLeft).toFixed(1)},` +
      `${deltaDbToY(clampDelta(d), height).toFixed(1)}`
    );
  }
  return seg.length > 1 ? `M ${seg.join(' L ')}` : '';
}
