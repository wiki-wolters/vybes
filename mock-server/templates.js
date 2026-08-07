/*
 * V1 preset templates - the factories that generate a preset's output
 * channels, crossover points and safety floors. Mirrors the design in
 * docs/CHANNEL_ARCHITECTURE.md. The ESP32-S3 firmware will carry the same
 * definitions; the contract tests treat this file's output as the spec.
 *
 * A template is a factory, not a mode: applying one generates the initial
 * config, after which every field remains editable (edits beyond what the
 * template's simple view can express flip preset.template to "custom").
 */

const NUM_OUTPUTS = 8;
const FIR_TAP_POOL = 12288;
const MAX_OUTPUT_PEQ = 10;
const MAX_INPUT_PEQ = 15;
const MAX_DELAY_US = 20000;
const CROSSOVER_TYPES = ['LR2', 'LR4', 'BW2'];

// Auto delay alignment probe chirp/schedule contract - must match the
// PROBE_* constants in ESP/esp-web-server/teensy_protocol.h.
const PROBE_SCHEDULE = {
  sampleRate: 44100,
  preRollSamples: 65536,
  spacingSamples: 49152,
  chirpSamples: 16384,
  tailSamples: 8192,
  fadeSamples: 512,
  f0: 60.0,
  f1: 8000.0,
};

// A disabled, silent output slot
function emptyOutput(index) {
  return {
    label: `Out ${index + 1}`,
    enabled: false,
    source: { left: 0, right: 0 },
    hp: { mode: 'off' },
    lp: { mode: 'off' },
    hpFloor: 0,
    peq: [],
    fir: '',
    delayUs: 0,
    gainDb: 0,
    invert: false,
    mute: false,
  };
}

function output(index, overrides) {
  return { ...emptyOutput(index), enabled: true, ...overrides };
}

const LEFT = { left: 1, right: 0 };
const RIGHT = { left: 0, right: 1 };
const MONO = { left: 0.5, right: 0.5 };

// Shared crossover point definitions. `locked: true` points are view-only in
// the UI and reject writes without confirm=true (driver protection).
const SUB_XO = { id: 'sub_xo', freq: 80, type: 'LR4', locked: false, min: 40, max: 500 };
const MID_XO = { id: 'mid_xo', freq: 400, type: 'LR4', locked: true, min: 100, max: 2000 };
const TWT_XO = { id: 'twt_xo', freq: 2500, type: 'LR4', locked: true, min: 800, max: 8000 };

const xover = (id) => ({ mode: 'xover', xover: id });

const TEMPLATES = {
  '2.0': {
    label: '2.0 Stereo',
    description: 'Left and right full-range speakers, no crossover.',
    crossovers: () => [],
    outputs: () => [
      output(0, { label: 'Left', source: LEFT }),
      output(1, { label: 'Right', source: RIGHT }),
    ],
  },
  '2.1': {
    label: '2.1 Stereo + Sub',
    description: 'Left and right speakers with a mono subwoofer and adjustable sub crossover.',
    crossovers: () => [{ ...SUB_XO }],
    outputs: () => [
      output(0, { label: 'Left', source: LEFT, hp: xover('sub_xo') }),
      output(1, { label: 'Right', source: RIGHT, hp: xover('sub_xo') }),
      output(2, { label: 'Sub', source: MONO, lp: xover('sub_xo') }),
    ],
  },
  '2.2': {
    label: '2.2 Stereo + Dual Subs',
    description: 'Left and right speakers with two mono subwoofers (independent gain and delay).',
    crossovers: () => [{ ...SUB_XO }],
    outputs: () => [
      output(0, { label: 'Left', source: LEFT, hp: xover('sub_xo') }),
      output(1, { label: 'Right', source: RIGHT, hp: xover('sub_xo') }),
      output(2, { label: 'Sub 1', source: MONO, lp: xover('sub_xo') }),
      output(3, { label: 'Sub 2', source: MONO, lp: xover('sub_xo') }),
    ],
  },
  '2way-sub': {
    label: '2-Way Active + Sub',
    description: 'Active 2-way speakers (woofer + tweeter per side) plus a mono subwoofer.',
    crossovers: () => [{ ...SUB_XO }, { ...TWT_XO }],
    outputs: () => [
      output(0, { label: 'L Woofer', source: LEFT, hp: xover('sub_xo'), lp: xover('twt_xo') }),
      output(1, { label: 'R Woofer', source: RIGHT, hp: xover('sub_xo'), lp: xover('twt_xo') }),
      output(2, { label: 'L Tweeter', source: LEFT, hp: xover('twt_xo'), hpFloor: 800 }),
      output(3, { label: 'R Tweeter', source: RIGHT, hp: xover('twt_xo'), hpFloor: 800 }),
      output(4, { label: 'Sub', source: MONO, lp: xover('sub_xo') }),
    ],
  },
  '3way': {
    label: '3-Way Active',
    description: 'Active 3-way speakers: low, mid and high driver per side.',
    crossovers: () => [{ ...MID_XO }, { ...TWT_XO }],
    outputs: () => [
      output(0, { label: 'L Low', source: LEFT, lp: xover('mid_xo') }),
      output(1, { label: 'R Low', source: RIGHT, lp: xover('mid_xo') }),
      output(2, { label: 'L Mid', source: LEFT, hp: xover('mid_xo'), lp: xover('twt_xo'), hpFloor: 100 }),
      output(3, { label: 'R Mid', source: RIGHT, hp: xover('mid_xo'), lp: xover('twt_xo'), hpFloor: 100 }),
      output(4, { label: 'L High', source: LEFT, hp: xover('twt_xo'), hpFloor: 800 }),
      output(5, { label: 'R High', source: RIGHT, hp: xover('twt_xo'), hpFloor: 800 }),
    ],
  },
  '3way-2sub': {
    label: '3-Way Active + Dual Subs',
    description: 'Active 3-way speakers plus two mono subwoofers - all eight outputs.',
    crossovers: () => [{ ...SUB_XO }, { ...MID_XO }, { ...TWT_XO }],
    outputs: () => [
      output(0, { label: 'L Low', source: LEFT, hp: xover('sub_xo'), lp: xover('mid_xo') }),
      output(1, { label: 'R Low', source: RIGHT, hp: xover('sub_xo'), lp: xover('mid_xo') }),
      output(2, { label: 'L Mid', source: LEFT, hp: xover('mid_xo'), lp: xover('twt_xo'), hpFloor: 100 }),
      output(3, { label: 'R Mid', source: RIGHT, hp: xover('mid_xo'), lp: xover('twt_xo'), hpFloor: 100 }),
      output(4, { label: 'L High', source: LEFT, hp: xover('twt_xo'), hpFloor: 800 }),
      output(5, { label: 'R High', source: RIGHT, hp: xover('twt_xo'), hpFloor: 800 }),
      output(6, { label: 'Sub 1', source: MONO, lp: xover('sub_xo') }),
      output(7, { label: 'Sub 2', source: MONO, lp: xover('sub_xo') }),
    ],
  },
};

const DEFAULT_TEMPLATE = '2.1';

// Default spl=0 input EQ set: three flat points (matches the old ESP
// handlePostPresetCreate defaults)
function defaultInputEq() {
  return {
    enabled: false,
    sets: [
      {
        spl: 0,
        points: [
          { freq: 100, gain: 0, q: 1 },
          { freq: 1000, gain: 0, q: 1 },
          { freq: 10000, gain: 0, q: 1 },
        ],
      },
    ],
  };
}

/**
 * Default dynamics (mixed-input multiband compressor) block. Must match
 * the ESP's Dynamics struct defaults (config.h).
 */
function defaultDynamics() {
  return {
    enabled: false,
    mode: 'voice',
    strength: 70,
    xoverLow: 250,
    xoverHigh: 4000,
    voicePriority: 6,
    bands: [
      { threshold: -24, ratio: 2, attack: 10, release: 150, makeup: 0, bypass: false },
      { threshold: -24, ratio: 2, attack: 10, release: 150, makeup: 0, bypass: false },
      { threshold: -24, ratio: 2, attack: 10, release: 150, makeup: 0, bypass: false },
    ],
  };
}

/**
 * Build a full V1 preset config object from a template id.
 * Throws on unknown template ids.
 */
function buildPresetConfig(templateId = DEFAULT_TEMPLATE) {
  const template = TEMPLATES[templateId];
  if (!template) {
    throw new Error(`Unknown template: ${templateId}`);
  }

  const outputs = template.outputs();
  // Pad to exactly NUM_OUTPUTS entries
  for (let i = outputs.length; i < NUM_OUTPUTS; i++) {
    outputs.push(emptyOutput(i));
  }

  return {
    template: templateId,
    crossovers: template.crossovers(),
    inputEq: defaultInputEq(),
    outputs,
    delaysEnabled: false,
    firEnabled: false,
    dynamics: defaultDynamics(),
  };
}

/** Template metadata for GET /templates */
function listTemplates() {
  return Object.entries(TEMPLATES).map(([id, t]) => ({
    id,
    label: t.label,
    description: t.description,
    outputsUsed: t.outputs().length,
  }));
}

module.exports = {
  NUM_OUTPUTS,
  FIR_TAP_POOL,
  MAX_OUTPUT_PEQ,
  MAX_INPUT_PEQ,
  MAX_DELAY_US,
  CROSSOVER_TYPES,
  PROBE_SCHEDULE,
  DEFAULT_TEMPLATE,
  buildPresetConfig,
  defaultDynamics,
  listTemplates,
};
