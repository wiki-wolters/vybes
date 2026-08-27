<template>
  <div v-if="open" class="capture-backdrop"></div>
  <div
    v-if="open"
    ref="dialogRef"
    class="capture-sheet"
    role="dialog"
    aria-modal="true"
    aria-label="Capture listening positions"
    tabindex="-1"
    @keydown.tab="wrapTab"
  >
    <!-- Header: what is being measured. Changing scope wipes every capture,
         so this is a standing reminder, not a control. -->
    <header class="capture-header">
      <div class="min-w-0">
        <p class="text-[11px] uppercase tracking-wide text-vybes-text-secondary">Measuring</p>
        <p class="text-sm font-semibold text-vybes-text-primary truncate">{{ scopeLabel }}</p>
      </div>
      <button class="btn-secondary shrink-0 px-3 py-1.5 text-sm" @click="$emit('close')">Done</button>
    </header>

    <!-- Deviation chart. Same scale as the page's chart (rta-chart.js), minus
         the crosshair, the EQ preview and the legend - none of which can be
         read while walking with the phone at arm's length. -->
    <div ref="chartContainer" class="capture-chart">
      <svg :width="width" :height="height" class="block">
        <g v-for="line in gridLines" :key="'g' + line.db">
          <line class="grid-line" :x1="PAD_LEFT" :y1="line.y" :x2="width" :y2="line.y" />
          <text class="grid-label" :x="4" :y="line.y + 3" font-size="9">
            {{ line.db > 0 ? '+' + line.db : line.db }}
          </text>
        </g>
        <line
          class="grid-line-strong"
          :x1="PAD_LEFT" :y1="zeroY" :x2="width" :y2="zeroY"
          stroke-width="1.5"
        />
        <rect
          v-for="bar in bars"
          :key="'b' + bar.index"
          :x="bar.x" :y="bar.y" :width="bar.w" :height="bar.h"
          :fill="bar.color" :opacity="bar.opacity" rx="1"
        />
        <path
          v-if="averagePath"
          class="trace-average"
          :d="averagePath"
          fill="none"
          stroke-width="2.5"
        />
        <!-- Outside the correction limits, dimmed - same window the page's
             chart shows, so a capture walk reads as the same measurement. -->
        <rect
          v-for="(shade, i) in shades"
          :key="'oob' + i"
          :x="shade.x" y="0" :width="shade.w" :height="height"
          fill="#000" opacity="0.5"
        />
      </svg>
    </div>
    <p class="capture-legend">
      <!-- One child: the box is a flex container (for vertical centring in a
           fixed height) and adjacent inline runs would become flex items with
           the whitespace between them collapsed away. -->
      <span>
        Live deviation, mic − source.
        <template v-if="captures.length">
          <span class="legend-average">━</span> average of {{ captures.length }} captured
          position{{ captures.length === 1 ? '' : 's' }}.
        </template>
        <template v-else>Bars above the line are what the room boosts.</template>
      </span>
    </p>

    <!-- The reason this modal exists: one target that never moves. -->
    <div class="capture-action">
      <button class="capture-button" :disabled="!ready" @click="$emit('capture')">
        <span class="capture-button-label">
          Capture position {{ captures.length + 1 }}
        </span>
        <span class="capture-button-sub">
          <template v-if="settling">
            hold still — {{ secondsRemaining }}s
          </template>
          <template v-else-if="!micActive">microphone is off</template>
          <template v-else-if="!sourceLive">waiting for the device…</template>
          <template v-else-if="ready">ready</template>
          <template v-else>waiting for a usable signal…</template>
        </span>
      </button>

      <!-- Settle progress. Without it the disabled button reads as broken:
           every capture restarts the window, so it goes dark again each time. -->
      <div class="capture-progress" :class="{ 'is-idle': !settling }">
        <div class="capture-progress-fill" :style="{ width: settleProgressPercent + '%' }"></div>
      </div>

      <!-- Fixed height, scrolled if it overflows: this sits below the button
           in a bottom-anchored stack, so anything that changes its height
           moves the button - which is the whole thing being fixed here. The
           first capture swapping the hint for a chips row was enough to shift
           it, and a chips row wrapping to a second line would do it again. -->
      <div class="capture-footer">
        <div v-if="captures.length" class="capture-chips">
          <span v-for="(c, i) in captures" :key="c.id" class="capture-chip">
            Position {{ i + 1 }}
            <button
              class="text-vybes-text-secondary hover:text-vybes-text-primary"
              :aria-label="`Remove position ${i + 1}`"
              @click="$emit('remove', c.id)"
            >✕</button>
          </span>
        </div>
        <p v-else class="text-xs text-vybes-text-secondary text-center">
          Stand where you listen, wait for the button, then move to the next spot.
        </p>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, watch, onMounted, onUnmounted, nextTick } from 'vue';
import {
  deltaZeroY,
  deltaDbToY,
  deviationBars,
  deviationPath,
  outOfBandShades,
} from '../rta-chart.js';

const props = defineProps({
  open: { type: Boolean, required: true },
  /** Band grid the delta/average arrays are on */
  grid: { type: Object, required: true },
  /** Live per-band deviation, or null while it is not usable */
  delta: { type: Array, default: null },
  /** Average of the captures so far, or null */
  average: { type: Array, default: null },
  captures: { type: Array, default: () => [] },
  ready: { type: Boolean, default: false },
  settling: { type: Boolean, default: false },
  micActive: { type: Boolean, default: false },
  sourceLive: { type: Boolean, default: false },
  /** Whole seconds left in the settle window */
  secondsRemaining: { type: Number, default: 0 },
  /** Length of the settle window, for the progress bar */
  averagingSeconds: { type: Number, default: 2 },
  scopeLabel: { type: String, default: '' },
  /** Correction limits, dimmed outside on the chart */
  loHz: { type: Number, default: 20 },
  hiHz: { type: Number, default: 20000 },
});

const emit = defineEmits(['close', 'capture', 'remove']);

const PAD_LEFT = 28;

const chartContainer = ref(null);
const dialogRef = ref(null);
// Both measured: the chart takes whatever the sheet has left over after the
// header and the capture button, which is most of a phone screen. A fixed
// height would leave the middle of the sheet empty, and this is a chart meant
// to be read at arm's length.
const width = ref(320);
const height = ref(150);

const zeroY = computed(() => deltaZeroY(height.value));
const gridLines = computed(() =>
  [-20, -10, 10, 20].map((db) => ({ db, y: deltaDbToY(db, height.value) }))
);

const bars = computed(() =>
  deviationBars(props.delta, props.grid, {
    width: width.value, padLeft: PAD_LEFT, height: height.value,
  })
);
const shades = computed(() =>
  outOfBandShades(props.loHz, props.hiHz, { width: width.value, padLeft: PAD_LEFT })
);
const averagePath = computed(() =>
  deviationPath(props.average, props.grid, {
    width: width.value, padLeft: PAD_LEFT, height: height.value,
  })
);

// Counts *up* to full: the bar filling is the wait ending. Empty whenever the
// window is not running, so it never sits at a misleading 100%.
const settleProgressPercent = computed(() => {
  if (!props.settling) return props.ready ? 100 : 0;
  const total = Math.max(props.averagingSeconds, 0.001);
  return Math.min(100, Math.max(0, (1 - props.secondsRemaining / total) * 100));
});

// --- Screen wake lock ---
// A position takes the settle window plus however long it takes to walk to the
// next spot, all of it without touching the screen. Losing the display halfway
// through means unlocking the phone mid-measurement, which moves the mic.
let wakeLock = null;
async function acquireWakeLock() {
  if (!navigator.wakeLock) return;
  try {
    wakeLock = await navigator.wakeLock.request('screen');
    // The OS drops the lock when the tab is hidden; it does not come back on
    // its own, so re-request when the page is shown again.
    wakeLock.addEventListener('release', () => { wakeLock = null; });
  } catch (e) {
    // Denied (low battery, no user gesture) - not worth surfacing.
    console.warn('Screen wake lock unavailable:', e);
  }
}
function releaseWakeLock() {
  wakeLock?.release?.().catch(() => {});
  wakeLock = null;
}
// On the window rather than the sheet, matching ModalDialog: Escape has to
// work even when focus has moved to a chip's remove button.
function onKeydown(event) {
  if (event.key === 'Escape') {
    event.preventDefault();
    emit('close');
  }
}

function onVisibilityChange() {
  if (document.visibilityState === 'visible' && props.open && !wakeLock) acquireWakeLock();
}

// --- Sizing / focus ---
let resizeObserver = null;
const measure = () => {
  const el = chartContainer.value;
  if (!el) return;
  if (el.clientWidth > 0) width.value = el.clientWidth;
  if (el.clientHeight > 0) height.value = el.clientHeight;
};
// ResizeObserver covers the sheet changing size in place; window resize covers
// the case this is actually used in - a phone rotating in your hand, where the
// observer's delivery is tied to rendering steps that a backgrounded tab may
// never run.
const onWindowResize = () => measure();

let previouslyFocused = null;
const FOCUSABLE =
  'a[href], button:not([disabled]), input:not([disabled]), select:not([disabled]), [tabindex]:not([tabindex="-1"])';

/** Minimal focus containment, matching ModalDialog's. */
function wrapTab(event) {
  const candidates = Array.from(dialogRef.value?.querySelectorAll(FOCUSABLE) ?? [])
    .filter((el) => el.offsetWidth > 0 || el.offsetHeight > 0);
  if (candidates.length === 0) return;
  const first = candidates[0];
  const last = candidates[candidates.length - 1];
  const active = document.activeElement;
  if (event.shiftKey && (active === first || active === dialogRef.value)) {
    event.preventDefault();
    last.focus();
  } else if (!event.shiftKey && active === last) {
    event.preventDefault();
    first.focus();
  }
}

async function activate() {
  previouslyFocused = document.activeElement;
  document.addEventListener('visibilitychange', onVisibilityChange);
  window.addEventListener('resize', onWindowResize);
  window.addEventListener('keydown', onKeydown);
  acquireWakeLock();
  await nextTick();
  measure();
  if (window.ResizeObserver && chartContainer.value) {
    resizeObserver = new ResizeObserver(measure);
    resizeObserver.observe(chartContainer.value);
  }
  // Deliberately not focusing the capture button: a stray Space or Return
  // would take a position the user never asked for.
  dialogRef.value?.focus();
}

function deactivate() {
  document.removeEventListener('visibilitychange', onVisibilityChange);
  window.removeEventListener('resize', onWindowResize);
  window.removeEventListener('keydown', onKeydown);
  releaseWakeLock();
  resizeObserver?.disconnect();
  resizeObserver = null;
  previouslyFocused?.focus?.();
  previouslyFocused = null;
}

watch(() => props.open, (open) => (open ? activate() : deactivate()));

// Mounting already-open is not just an HMR artifact - it is the only path that
// skips the watcher, and skipping it leaves the chart stuck at its placeholder
// size with no observer attached to correct it.
onMounted(() => {
  if (props.open) activate();
});

onUnmounted(deactivate);
</script>

<style scoped>
.capture-backdrop {
  position: fixed;
  inset: 0;
  background-color: rgba(0, 0, 0, 0.75);
  z-index: 50;
}

/* Full-bleed rather than a centred card: this is used one-handed, at arm's
   length, while walking. Anything the page would show around it is noise. */
.capture-sheet {
  position: fixed;
  inset: 0;
  z-index: 51;
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
  padding: 1rem;
  padding-top: max(1rem, env(safe-area-inset-top));
  padding-bottom: max(1rem, env(safe-area-inset-bottom));
  background-color: var(--vybes-dark-bg, #0b0f14);
  overflow-y: auto;
}

@media (min-width: 640px) {
  /* On a desktop the full screen is absurd; keep the same stacking order in a
     sheet wide enough to read but no wider. */
  .capture-sheet {
    inset: 50% auto auto 50%;
    transform: translate(-50%, -50%);
    width: min(560px, 92vw);
    max-height: 90vh;
    border: 1px solid var(--vybes-border);
    border-radius: 0.75rem;
    box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.5);
  }
}

.capture-sheet:focus {
  outline: none;
}

.capture-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 1rem;
}

.capture-chart {
  width: 100%;
  /* No max: the chart takes every pixel the rest of the sheet does not, which
     is what pins the button to a fixed distance from the bottom. Cap it and
     the slack reappears above the button, which then moves again whenever
     something above it reflows. */
  flex: 1 1 auto;
  min-height: 7rem;
  border-radius: 0.25rem;
  background-color: rgba(0, 0, 0, 0.3);
  overflow: hidden;
}

/* Fixed two-line box: the wording changes on the first capture, and letting
   that reflow would resize the chart under the user mid-measurement. */
.capture-legend {
  margin-top: -0.25rem;
  height: 2.25rem;
  display: flex;
  align-items: center;
  font-size: 11px;
  color: var(--vybes-text-secondary);
}

/* Last in the flow, under a chart that absorbs all the spare height: thumb
   reach on a phone, and a fixed distance from the bottom whatever the chips
   row is doing. */
.capture-action {
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
}

.capture-button {
  width: 100%;
  min-height: 8rem;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 0.35rem;
  border-radius: 1rem;
  border: 1px solid transparent;
  background-color: var(--vybes-brand);
  color: #04121a;
  font-weight: 700;
  transition: background-color 120ms ease, opacity 120ms ease;
}

.capture-button:disabled {
  background-color: transparent;
  border-color: var(--vybes-border);
  color: var(--vybes-text-secondary);
}

.capture-button-label {
  font-size: 1.375rem;
  line-height: 1.1;
}

.capture-button-sub {
  font-size: 0.8125rem;
  font-weight: 500;
  opacity: 0.8;
  font-variant-numeric: tabular-nums;
}

.capture-progress {
  height: 4px;
  border-radius: 999px;
  background-color: rgba(255, 255, 255, 0.1);
  overflow: hidden;
}

.capture-progress.is-idle {
  opacity: 0.35;
}

.capture-progress-fill {
  height: 100%;
  background-color: var(--vybes-brand);
  transition: width 200ms linear;
}

.capture-footer {
  height: 3.5rem;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  justify-content: center;
}

.capture-chips {
  display: flex;
  flex-wrap: wrap;
  gap: 0.5rem;
  justify-content: center;
}

.capture-chip {
  display: inline-flex;
  align-items: center;
  gap: 0.375rem;
  padding: 0.25rem 0.625rem;
  border-radius: 999px;
  font-size: 0.75rem;
  background-color: rgba(0, 0, 0, 0.3);
  border: 1px solid var(--vybes-border);
  color: var(--vybes-text-primary);
}

.grid-line {
  stroke: var(--vybes-border);
  stroke-width: 1;
  opacity: 0.5;
}

.grid-line-strong {
  stroke: var(--vybes-text-secondary);
  opacity: 0.6;
}

.grid-label {
  fill: var(--vybes-text-secondary);
}

.trace-average {
  stroke: #a78bfa; /* violet: matches the page chart's captured average */
}

.legend-average {
  color: #a78bfa;
}
</style>
