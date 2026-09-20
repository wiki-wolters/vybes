<template>
  <div>
    <div class="flex flex-wrap items-center justify-between gap-x-3 gap-y-1 mb-1">
      <span class="text-xs font-medium text-vybes-text-secondary">
        Harmonic distortion, relative to the fundamental
      </span>
      <span class="flex flex-wrap items-center gap-x-3 gap-y-1 text-xs text-vybes-text-secondary">
        <span class="flex items-center gap-1">
          <span class="legend-swatch" :style="{ background: SERIES[0].color }"></span>total
        </span>
        <span v-for="s in orderSeries" :key="s.key" class="flex items-center gap-1">
          <span class="legend-swatch" :style="{ background: s.color }"></span>{{ s.key }}
        </span>
        <span class="flex items-center gap-1"><span class="legend-swatch legend-swatch-floor"></span>floor</span>
      </span>
    </div>

    <svg :viewBox="`0 0 ${width} ${height}`" class="w-full" :style="{ height: height + 'px' }">
      <rect v-if="bandRect" :x="bandRect.x" y="0" :width="bandRect.w" :height="height" class="band-shade" />
      <line v-for="g in gridFreqs" :key="'g' + g.freq" :x1="g.x" :x2="g.x" y1="0" :y2="plotBottom" class="grid-line" />
      <g v-for="l in dbGrid" :key="'d' + l.db">
        <line :x1="plotLeft" :x2="width" :y1="l.y" :y2="l.y" class="grid-line" />
        <text x="2" :y="l.y + 3" class="axis-label axis-label-left">{{ l.label }}</text>
      </g>

      <path v-if="floorPath" :d="floorPath" class="trace-floor" />
      <path
        v-for="s in orderSeries" :key="'p' + s.key"
        :d="s.path" class="trace-order" :style="{ stroke: s.color }"
      />
      <path :d="thdPath" class="trace-total" :style="{ stroke: SERIES[0].color }" />

      <!-- Direct labels: the legend alone leaves identity on colour, and the
           closest adjacent pair here is at the CVD floor. -->
      <text
        v-for="s in orderSeries" :key="'l' + s.key"
        v-show="s.label" :x="s.label?.x" :y="s.label?.y"
        class="series-label" :style="{ fill: s.color }"
      >{{ s.key }}</text>

      <text v-for="g in gridFreqs" :key="'gl' + g.freq" :x="g.x" :y="height - 3" class="axis-label">
        {{ g.label }}
      </text>
    </svg>

    <p class="text-xs text-vybes-text-secondary mt-1 tabular-nums">
      <template v-if="worst">
        Worst {{ worst.percent.toFixed(2) }}% ({{ worst.db.toFixed(0) }} dB) at
        {{ formatFreq(worst.freq) }}, mostly {{ worst.order }} &mdash; radiating around
        {{ formatFreq(worst.radiatedHz) }}.
      </template>
      <template v-else>No harmonic reading available for this output.</template>
    </p>
  </div>
</template>

<script setup>
import { computed } from 'vue';
import { relDbToPercent } from '../../sweep-math.js';

// Traces a harmonicResponse() result ({freqs, orders, thdDb, floorDb}) on a
// log-frequency axis. Presentation only - all the math lives in
// sweep-math.js, same split as FirResponseChart.
//
// The x axis is the FUNDAMENTAL frequency, which is what makes the chart
// actionable: a cone breaking up at 1.2kHz shows as a bump at 1.2kHz on the
// 3rd-order trace, even though the energy you hear arrives at 3.6kHz. The
// summary line under the chart names both.
const props = defineProps({
  harmonics: { type: Object, required: true },
  band: { type: Object, default: null },
  width: { type: Number, default: 600 },
  height: { type: Number, default: 150 },
  floorDb: { type: Number, default: -90 }, // bottom of the y axis
});

// Fixed categorical order (dataviz skill's default theme, dark steps),
// validated against this app's card surface: total, then ascending order.
const SERIES = [
  { key: 'THD', color: '#3987e5' },
  { key: 'H2', color: '#d95926' },
  { key: 'H3', color: '#199e70' },
  { key: 'H4', color: '#c98500' },
  { key: 'H5', color: '#d55181' },
];

const plotTop = 6;
// The dB scale needs somewhere to live that isn't on top of the traces, and
// the lowest frequency label needs room to centre on its gridline.
const plotLeft = 62; // fits the widest tick label, "-80 dB . 0.01%"
const plotBottom = computed(() => props.height - 14);

const freqBounds = computed(() => {
  const f = props.harmonics.freqs;
  return { lo: Math.log10(f[0]), hi: Math.log10(f[f.length - 1]) };
});

function xAt(freq) {
  const { lo, hi } = freqBounds.value;
  const t = (Math.log10(freq) - lo) / (hi - lo || 1);
  const span = props.width - plotLeft;
  return plotLeft + Math.max(0, Math.min(span, t * span));
}

function yAt(db) {
  const clamped = Math.max(props.floorDb, Math.min(0, db));
  const t = clamped / props.floorDb; // 0 at 0dB, 1 at the axis floor
  return plotTop + t * (plotBottom.value - plotTop);
}

function tracePath(values) {
  const { freqs } = props.harmonics;
  const seg = [];
  for (let i = 0; i < freqs.length; i++) {
    if (!Number.isFinite(values[i])) continue;
    seg.push(`${xAt(freqs[i]).toFixed(1)},${yAt(values[i]).toFixed(1)}`);
  }
  return seg.length > 1 ? `M ${seg.join(' L ')}` : '';
}

const thdPath = computed(() => tracePath(props.harmonics.thdDb));
const floorPath = computed(() => tracePath(props.harmonics.floorDb));

const orderSeries = computed(() =>
  props.harmonics.orders.map((row, i) => {
    const color = SERIES[i + 1]?.color ?? SERIES[SERIES.length - 1].color;
    // Direct label where this order is loudest: it spreads the labels out
    // instead of stacking them all against the right edge, and it points at
    // the feature worth reading.
    let peak = -1;
    for (let j = 0; j < props.harmonics.freqs.length; j++) {
      if (!Number.isFinite(row.relDb[j])) continue;
      if (peak < 0 || row.relDb[j] > row.relDb[peak]) peak = j;
    }
    const buried = peak < 0 || (Number.isFinite(props.harmonics.floorDb[peak]) &&
      row.relDb[peak] < props.harmonics.floorDb[peak] + 3);
    const label = buried ? null : {
      x: Math.max(plotLeft + 2, Math.min(xAt(props.harmonics.freqs[peak]) + 4, props.width - 16)),
      y: Math.max(10, Math.min(yAt(row.relDb[peak]) - 4, plotBottom.value - 2)),
    };
    return { key: `H${row.order}`, color, path: tracePath(row.relDb), label };
  })
);

// -20dB = 10%, -40 = 1%, -60 = 0.1%: the two scales people quote distortion
// in, on one set of lines, so neither needs a second axis.
const dbGrid = computed(() =>
  [-20, -40, -60, -80]
    .filter((db) => db > props.floorDb)
    .map((db) => ({ db, y: yAt(db), label: `${db} dB · ${trimPercent(relDbToPercent(db))}%` }))
);

function trimPercent(p) {
  if (p >= 1) return p.toFixed(0);
  if (p >= 0.1) return p.toFixed(1);
  return p.toFixed(2);
}

function formatFreq(f) {
  return f >= 1000 ? `${(f / 1000).toFixed(f >= 10000 ? 0 : 1)} kHz` : `${Math.round(f)} Hz`;
}

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

// The headline: where total distortion peaks, which order carries it, and
// the frequency that energy actually comes out at.
const worst = computed(() => {
  const { freqs, thdDb, orders } = props.harmonics;
  let best = -1;
  for (let i = 0; i < freqs.length; i++) {
    if (!Number.isFinite(thdDb[i])) continue;
    if (best < 0 || thdDb[i] > thdDb[best]) best = i;
  }
  if (best < 0) return null;
  let top = orders[0];
  for (const row of orders) {
    if (Number.isFinite(row.relDb[best]) &&
        (!Number.isFinite(top.relDb[best]) || row.relDb[best] > top.relDb[best])) {
      top = row;
    }
  }
  return {
    freq: freqs[best],
    db: thdDb[best],
    percent: relDbToPercent(thdDb[best]),
    order: `H${top.order}`,
    radiatedHz: freqs[best] * top.order,
  };
});
</script>

<style scoped>
.grid-line {
  stroke: var(--vybes-border);
  stroke-width: 1;
  opacity: 0.4;
}
.band-shade {
  fill: var(--vybes-primary);
  opacity: 0.08;
}
.trace-total {
  fill: none;
  stroke-width: 2;
}
.trace-order {
  fill: none;
  stroke-width: 1.5;
}
.trace-floor {
  fill: none;
  stroke: var(--vybes-text-secondary);
  stroke-width: 1.25;
  stroke-dasharray: 4 3;
  opacity: 0.7;
}
.axis-label {
  fill: var(--vybes-text-secondary);
  font-size: 9px;
  text-anchor: middle;
}
.axis-label-left {
  text-anchor: start;
}
.series-label {
  font-size: 9px;
  font-weight: 600;
}
.legend-swatch {
  display: inline-block;
  width: 8px;
  height: 8px;
  border-radius: 2px;
}
.legend-swatch-floor {
  display: inline-block;
  width: 8px;
  height: 0;
  border-top: 2px dashed var(--vybes-text-secondary);
}
</style>
