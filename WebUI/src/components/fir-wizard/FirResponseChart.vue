<template>
  <div>
    <div class="flex items-center justify-between mb-1">
      <span class="text-xs font-medium text-vybes-text-secondary">Magnitude (dB)</span>
      <span v-if="showLegend" class="flex items-center gap-3 text-xs text-vybes-text-secondary">
        <span class="flex items-center gap-1"><span class="legend-swatch" style="background:var(--vybes-primary)"></span>measured</span>
        <span v-if="compareMeasurement" class="flex items-center gap-1"><span class="legend-swatch" style="background:var(--vybes-accent)"></span>corrected</span>
        <span v-if="targetPath" class="flex items-center gap-1"><span class="legend-swatch legend-swatch-target"></span>target</span>
      </span>
    </div>
    <svg :viewBox="`0 0 ${width} ${magHeight}`" class="w-full" :style="{ height: magHeight + 'px' }">
      <rect v-if="bandRect" :x="bandRect.x" y="0" :width="bandRect.w" :height="magHeight" class="band-shade" />
      <line v-for="g in gridFreqs" :key="'mg' + g.freq" :x1="g.x" :x2="g.x" y1="0" :y2="magHeight" class="grid-line" />
      <line x1="0" :x2="width" :y1="magZeroY" :y2="magZeroY" class="grid-line-strong" />
      <path v-if="targetPath" :d="targetPath" class="trace-target" />
      <path v-if="compareMagPath" :d="compareMagPath" class="trace-compare" />
      <path :d="magPath" class="trace-primary" />
      <text v-for="g in gridFreqs" :key="'mgl' + g.freq" :x="g.x" :y="magHeight - 3" class="axis-label">{{ g.label }}</text>
    </svg>

    <div class="flex items-center justify-between mt-3 mb-1">
      <span class="text-xs font-medium text-vybes-text-secondary">Excess phase (deg)</span>
    </div>
    <svg :viewBox="`0 0 ${width} ${phaseHeight}`" class="w-full" :style="{ height: phaseHeight + 'px' }">
      <rect v-if="bandRect" :x="bandRect.x" y="0" :width="bandRect.w" :height="phaseHeight" class="band-shade" />
      <line x1="0" :x2="width" :y1="phaseHeight / 2" :y2="phaseHeight / 2" class="grid-line-strong" />
      <path v-if="comparePhasePath" :d="comparePhasePath" class="trace-compare" />
      <path :d="phasePath" class="trace-primary" />
    </svg>
  </div>
</template>

<script setup>
import { computed } from 'vue';

// Self-contained log-frequency SVG traces for a gatedResponse-shaped
// measurement ({freqs, magDb, excessPhaseRad}). Deliberately simple (per the
// slice C brief: "inline SVG is fine") - this is presentation only, all the
// actual math lives in sweep-math.js/fir-design.js/fir-wizard.js.
const props = defineProps({
  measurement: { type: Object, required: true },
  compareMeasurement: { type: Object, default: null },
  // House-curve target on the measurement's own frequencies (fir-wizard.js
  // targetDbForOutput); absolute level is meaningless, see targetPath.
  targetDb: { type: [Array, Float64Array], default: null },
  band: { type: Object, default: null }, // {fLo, fHi} shaded for context
  showLegend: { type: Boolean, default: false },
  width: { type: Number, default: 600 },
  magHeight: { type: Number, default: 140 },
  phaseHeight: { type: Number, default: 100 },
  magRangeDb: { type: Number, default: 15 }, // +/- range around the mean
  phaseRangeDeg: { type: Number, default: 180 },
});

const freqBounds = computed(() => {
  const f = props.measurement.freqs;
  return { lo: Math.log10(f[0]), hi: Math.log10(f[f.length - 1]) };
});

function xAt(freq) {
  const { lo, hi } = freqBounds.value;
  const t = (Math.log10(freq) - lo) / (hi - lo || 1);
  return Math.max(0, Math.min(props.width, t * props.width));
}

const magMean = computed(() => {
  const m = props.measurement.magDb;
  let s = 0;
  for (let i = 0; i < m.length; i++) s += m[i];
  return m.length ? s / m.length : 0;
});

const magZeroY = computed(() => props.magHeight / 2);

function magY(db) {
  const clamped = Math.max(-props.magRangeDb, Math.min(props.magRangeDb, db - magMean.value));
  return magZeroY.value - (clamped / props.magRangeDb) * (props.magHeight / 2 - 6);
}

function phaseY(rad) {
  const deg = (rad * 180) / Math.PI;
  const clamped = Math.max(-props.phaseRangeDeg, Math.min(props.phaseRangeDeg, deg));
  return props.phaseHeight / 2 - (clamped / props.phaseRangeDeg) * (props.phaseHeight / 2 - 6);
}

function tracePath(freqs, values, yFn) {
  const seg = [];
  for (let i = 0; i < freqs.length; i++) {
    if (!Number.isFinite(values[i])) continue;
    seg.push(`${xAt(freqs[i]).toFixed(1)},${yFn(values[i]).toFixed(1)}`);
  }
  return seg.length > 1 ? `M ${seg.join(' L ')}` : '';
}

const magPath = computed(() => tracePath(props.measurement.freqs, props.measurement.magDb, magY));
const phasePath = computed(() => tracePath(props.measurement.freqs, props.measurement.excessPhaseRad, phaseY));
const compareMagPath = computed(() =>
  props.compareMeasurement ? tracePath(props.compareMeasurement.freqs, props.compareMeasurement.magDb, magY) : ''
);
const comparePhasePath = computed(() =>
  props.compareMeasurement
    ? tracePath(props.compareMeasurement.freqs, props.compareMeasurement.excessPhaseRad, phaseY)
    : ''
);

// The target's own level is arbitrary - designKernel aims the correction at
// the target plus the in-band median of (measurement - target), so drawing
// it needs that same offset, or a curve the kernel matches perfectly would
// appear to sit tens of dB away from the corrected trace.
const targetPath = computed(() => {
  const t = props.targetDb;
  if (!t || t.length !== props.measurement.freqs.length) return '';
  const { freqs, magDb } = props.measurement;
  const deviations = [];
  for (let i = 0; i < freqs.length; i++) {
    if (!props.band || (freqs[i] >= props.band.fLo && freqs[i] <= props.band.fHi)) {
      deviations.push(magDb[i] - t[i]);
    }
  }
  deviations.sort((a, b) => a - b);
  const ref = deviations.length ? deviations[(deviations.length - 1) >> 1] : 0;
  return tracePath(freqs, Array.from(t, (v) => v + ref), magY);
});

// A few round-number gridlines across whatever span this chart covers.
const gridFreqs = computed(() => {
  const candidates = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000];
  const { lo, hi } = freqBounds.value;
  return candidates
    .filter((f) => Math.log10(f) >= lo && Math.log10(f) <= hi)
    .map((f) => ({ freq: f, x: xAt(f), label: f >= 1000 ? `${f / 1000}k` : `${f}` }));
});

const bandRect = computed(() => {
  if (!props.band) return null;
  const x0 = xAt(props.band.fLo);
  const x1 = xAt(props.band.fHi);
  return { x: Math.min(x0, x1), w: Math.max(1, Math.abs(x1 - x0)) };
});
</script>

<style scoped>
.grid-line {
  stroke: var(--vybes-border);
  stroke-width: 1;
  opacity: 0.4;
}
.grid-line-strong {
  stroke: var(--vybes-border);
  stroke-width: 1;
  opacity: 0.8;
}
.band-shade {
  fill: var(--vybes-primary);
  opacity: 0.08;
}
.trace-primary {
  fill: none;
  stroke: var(--vybes-primary);
  stroke-width: 1.75;
}
.trace-compare {
  fill: none;
  stroke: var(--vybes-accent);
  stroke-width: 1.75;
}
.trace-target {
  fill: none;
  stroke: var(--vybes-text-secondary);
  stroke-width: 1.25;
  stroke-dasharray: 4 3;
}
.axis-label {
  fill: var(--vybes-text-secondary);
  font-size: 9px;
  text-anchor: middle;
}
.legend-swatch {
  display: inline-block;
  width: 8px;
  height: 8px;
  border-radius: 2px;
}
.legend-swatch-target {
  height: 0;
  border-top: 2px dashed var(--vybes-text-secondary);
  border-radius: 0;
}
</style>
