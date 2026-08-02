/*
 * Dynamics (mixed-input multiband compressor) factory modes and helpers.
 *
 * A mode is a complete parameter set for the 3-band compressor; the UI's
 * simple tier is "pick a mode, set strength". The numbers here are UI-side
 * only - adding or tuning a mode never needs a firmware release. The
 * device runs whatever the preset stores; `mode` is just a label telling
 * the UI which chip to highlight.
 */

export const COMP_BAND_NAMES = ['Bass', 'Mid · voice', 'Treble'];

// Shared band-split defaults: 250Hz keeps voice fundamentals out of the
// bass band, 4kHz keeps presence/sibilance in its own band.
const SPLITS = { xoverLow: 250, xoverHigh: 4000 };

export const DYNAMICS_MODES = [
  {
    id: 'voice',
    label: 'Voice focus',
    hint: 'Keeps dialogue and comms steady; clamps bass-heavy peaks like explosions.',
    settings: {
      ...SPLITS,
      voicePriority: 6,
      bands: [
        // Bass: hard and fast - explosions, rumble
        { threshold: -24, ratio: 6, attack: 5, release: 150, makeup: 0, bypass: false },
        // Mid: barely compressed, lifted slightly so voice rides forward
        { threshold: -30, ratio: 1.5, attack: 15, release: 200, makeup: 2, bypass: false },
        // Treble: moderate - sharp cracks, debris, sibilance
        { threshold: -27, ratio: 3, attack: 8, release: 120, makeup: 0, bypass: false },
      ],
    },
  },
  {
    id: 'night',
    label: 'Night',
    hint: 'Squashes everything gently - big moments stay quiet, quiet moments stay audible.',
    settings: {
      ...SPLITS,
      voicePriority: 0,
      bands: [
        { threshold: -32, ratio: 4, attack: 10, release: 400, makeup: 4, bypass: false },
        { threshold: -34, ratio: 3, attack: 15, release: 400, makeup: 4, bypass: false },
        { threshold: -32, ratio: 4, attack: 10, release: 400, makeup: 3, bypass: false },
      ],
    },
  },
  {
    id: 'punch',
    label: 'Punch',
    hint: 'Slow attack lets transients hit, then reins in the tail.',
    settings: {
      ...SPLITS,
      voicePriority: 0,
      bands: [
        { threshold: -26, ratio: 4, attack: 35, release: 180, makeup: 2, bypass: false },
        { threshold: -28, ratio: 2.5, attack: 30, release: 200, makeup: 1, bypass: false },
        { threshold: -26, ratio: 3, attack: 25, release: 150, makeup: 1, bypass: false },
      ],
    },
  },
];

export function dynamicsModeById(id) {
  return DYNAMICS_MODES.find((m) => m.id === id) || null;
}

// A dynamics block whose numeric parameters deviate from its mode's
// definition is "custom" (the user edited the advanced view). Strength and
// enabled are user knobs in every mode, so they don't count.
export function matchesMode(dynamics, mode) {
  if (!mode) return false;
  const s = mode.settings;
  const close = (a, b) => Math.abs(a - b) < 0.05;
  if (!close(dynamics.xoverLow, s.xoverLow) || !close(dynamics.xoverHigh, s.xoverHigh)) return false;
  if (!close(dynamics.voicePriority, s.voicePriority)) return false;
  return dynamics.bands.every((band, i) => {
    const ref = s.bands[i];
    return close(band.threshold, ref.threshold) && close(band.ratio, ref.ratio) &&
      close(band.attack, ref.attack) && close(band.release, ref.release) &&
      close(band.makeup, ref.makeup) && !band.bypass === !ref.bypass;
  });
}

// Decode a GRM websocket frame (6 hex chars) into per-band dB of reduction
export function decodeGrmFrame(hex) {
  if (typeof hex !== 'string' || hex.length < 6) return null;
  const out = [0, 0, 0];
  for (let i = 0; i < 3; i++) {
    const v = parseInt(hex.substr(i * 2, 2), 16);
    if (Number.isNaN(v)) return null;
    out[i] = v / 8;
  }
  return out;
}
