<template>
  <div class="parametric-eq" :class="{ 'is-fullscreen': isFullscreen }">
    <div class="eq-container" ref="eqContainer">
      <svg
        ref="svgEl"
        class="eq-svg"
        :width="width"
        :height="height"
        @pointerdown="onPointerDown"
        @pointermove="onPointerMove"
        @pointerup="onPointerUp"
        @pointercancel="onPointerCancel"
        @wheel.prevent="onWheel"
      >
        <defs>
          <linearGradient id="vybes-eq-fill" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0" stop-color="rgba(245, 192, 78, 0.25)" />
            <stop offset="1" stop-color="rgba(245, 192, 78, 0.02)" />
          </linearGradient>
        </defs>

        <!-- Grid -->
        <g class="grid">
          <line
            v-for="f in freqMinorLines"
            :key="`fmin-${f.value}`"
            :x1="f.x" :y1="0" :x2="f.x" :y2="height"
            stroke="rgba(148, 168, 196, 0.06)" stroke-width="1"
          />
          <line
            v-for="f in freqMajorLines"
            :key="`fmaj-${f.value}`"
            :x1="f.x" :y1="0" :x2="f.x" :y2="height"
            stroke="rgba(148, 168, 196, 0.16)" stroke-width="1"
          />
          <text
            v-for="f in freqMajorLines"
            :key="`flab-${f.value}`"
            :x="f.x" :y="height - 8"
            fill="#5d6878" font-size="10" text-anchor="middle"
          >
            {{ f.label }}
          </text>
          <line
            v-for="g in gainGridLines"
            :key="`g-${g.value}`"
            :x1="0" :y1="g.y" :x2="width" :y2="g.y"
            :stroke="g.value === 0 ? 'rgba(148, 168, 196, 0.30)' : 'rgba(148, 168, 196, 0.07)'"
            :stroke-width="g.value === 0 ? 1.5 : 1"
          />
          <text
            v-for="g in gainGridLines.filter(g => g.value !== -15)"
            :key="`glab-${g.value}`"
            :x="8" :y="g.y - 4"
            fill="#5d6878" font-size="10"
          >
            {{ g.label }}
          </text>
        </g>

        <!-- Per-band response curves -->
        <path
          v-for="(d, i) in bandPaths"
          :key="`band-${i}`"
          :d="d"
          fill="none"
          :stroke="bandColor(i)"
          :stroke-width="i === selectedPoint ? 1.8 : 1.2"
          :opacity="i === selectedPoint ? 0.85 : 0.35"
        />

        <!-- Combined curve with fill against the 0 dB line -->
        <path :d="combinedFillPath" fill="url(#vybes-eq-fill)" stroke="none" />
        <path
          :d="combinedPath"
          fill="none"
          stroke="#f5c04e"
          stroke-width="2.5"
          stroke-linejoin="round"
        />

        <!-- Band nodes -->
        <g
          v-for="(point, i) in localEqPoints"
          :key="`node-${i}`"
          class="eq-node"
          :data-index="i"
        >
          <circle
            :cx="frequencyToX(point.freq)" :cy="gainToY(point.gain)"
            r="20" fill="transparent"
          />
          <circle
            v-if="i === selectedPoint"
            :cx="frequencyToX(point.freq)" :cy="gainToY(point.gain)"
            r="13" fill="none" :stroke="bandColor(i)" stroke-width="2" opacity="0.45"
          />
          <circle
            :cx="frequencyToX(point.freq)" :cy="gainToY(point.gain)"
            :r="i === selectedPoint ? 8 : 6.5"
            :fill="bandColor(i)" stroke="#10141a" stroke-width="2"
          />
          <text
            :x="frequencyToX(point.freq)" :y="gainToY(point.gain)"
            fill="#10141a" font-size="9" font-weight="700"
            text-anchor="middle" dy="3" pointer-events="none"
          >
            {{ i + 1 }}
          </text>
        </g>
      </svg>

      <!-- Drag/pinch readout -->
      <div
        class="drag-readout"
        :class="{ show: readout.show }"
        :style="{ left: readout.x + 'px', top: readout.y + 'px' }"
      >
        {{ fmtFreq(readout.freq) }}<br>
        {{ fmtGain(readout.gain) }} · Q {{ readout.q.toFixed(2) }}
      </div>
    </div>

    <!-- Band rail: horizontally scrollable chips -->
    <div v-if="!isFullscreen" class="point-selection" ref="chipRail">
      <button
        v-for="(point, index) in localEqPoints"
        :key="`chip-${index}`"
        type="button"
        class="point-chip"
        :class="{ active: selectedPoint === index }"
        :style="{ '--chip-color': bandColor(index) }"
        @click="selectedPoint = index"
      >
        <span class="chip-dot" :style="{ background: bandColor(index) }"></span>
        <span>{{ fmtFreq(point.freq) }} · {{ fmtGain(point.gain) }}</span>
      </button>
      <button
        v-if="localEqPoints.length < MAX_POINTS"
        type="button"
        class="point-chip chip-ghost"
        @click="addPoint"
      >
        + Add band
      </button>
      <button type="button" class="point-chip chip-ghost" @click="openImport">
        Import REW
      </button>
    </div>

    <!-- Selected band controls -->
    <div
      v-if="!isFullscreen && selectedPoint !== null && localEqPoints[selectedPoint]"
      class="eq-controls"
    >
      <div class="eq-controls-head">
        <span class="chip-dot" :style="{ background: bandColor(selectedPoint) }"></span>
        <h4>Band {{ selectedPoint + 1 }}</h4>
        <button type="button" class="delete-band-btn" @click="removePoint(selectedPoint)">
          Delete band
        </button>
      </div>
      <div class="control-group">
        <range-slider
          label="Frequency"
          :min="20"
          :max="20000"
          :step="1"
          unit="Hz"
          :decimals="0"
          :logarithmic="true"
          v-model="localEqPoints[selectedPoint].freq"
          @update:modelValue="() => requestUpdate()"
        />
      </div>
      <div class="control-group">
        <range-slider
          label="Gain"
          :min="-15"
          :max="15"
          :step="0.1"
          unit="dB"
          :decimals="2"
          v-model="localEqPoints[selectedPoint].gain"
          @update:modelValue="() => requestUpdate()"
        />
      </div>
      <div class="control-group">
        <range-slider
          label="Q Factor"
          :min="0.1"
          :max="10"
          :step="0.1"
          unit=""
          :decimals="3"
          :logarithmic="true"
          v-model="localEqPoints[selectedPoint].q"
          @update:modelValue="() => requestUpdate()"
        />
      </div>
    </div>

    <modal-dialog
      v-model="showImportModal"
      title="Import PEQ Settings"
      confirm-text="Import"
      @confirm="handleImport"
    >
      <p class="mb-3 text-sm text-gray-400">
        Paste REW's "Export filter settings as text" output below, or choose the exported .txt file.
        Peaking (PK) filters are imported, up to {{ MAX_POINTS }} bands.
      </p>
      <textarea
        v-model="importText"
        class="w-full h-40 p-2 border border-gray-600 rounded bg-gray-800 text-white font-mono text-xs"
        placeholder="Filter 1: ON PK Fc 63.5 Hz Gain -4.5 dB Q 4.32"
        spellcheck="false"
      ></textarea>
      <div class="flex items-center justify-between mt-2">
        <button type="button" class="import-file-btn" @click="importFileInput.click()">
          Choose file…
        </button>
        <span
          class="text-xs"
          :class="importParsedPoints.length ? 'text-green-400' : 'text-red-400'"
        >
          <template v-if="importText.trim()">
            {{ importParsedPoints.length
              ? `${importParsedPoints.length} filter${importParsedPoints.length === 1 ? '' : 's'} found`
              : 'No PK filters recognised' }}
          </template>
        </span>
      </div>
      <input
        ref="importFileInput"
        type="file"
        accept=".txt,text/plain"
        hidden
        @change="onImportFile"
      >
    </modal-dialog>

  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted, onUnmounted, watch, nextTick } from 'vue';
import RangeSlider from './shared/RangeSlider.vue';
import ModalDialog from './shared/ModalDialog.vue';
import VybesAPI from '../api-client';
import { peakingBellDb } from '../eq-math.js';

const props = defineProps({
  peqPoints: {
    type: Array,
    default: () => [
      { id: 1, freq: 100, gain: 0, q: 1 },
      { id: 2, freq: 1000, gain: 0, q: 1 },
      { id: 3, freq: 10000, gain: 0, q: 1 }
    ]
  },
  presetName: {
    type: String,
    required: true
  },
  eqType: {
    type: String,
    default: 'pref' // Assuming this component is primarily for preference EQ
  }
});

const emit = defineEmits(['change']);

const MAX_POINTS = 15;
const MIN_FREQ = 20;
const MAX_FREQ = 20000;
const MAX_GAIN = 15;
const MIN_Q = 0.1;
const MAX_Q = 10;

// One hue per band, shared by its node, curve and chip so bands stay
// identifiable when many are active.
const BAND_COLORS = [
  '#4ea1ff', '#ff9e64', '#7bd88f', '#ff6b9d', '#b28cff',
  '#3fd2c7', '#f95f53', '#6ee7ff', '#c3e88d', '#eeb86d',
  '#8aadf4', '#ee99a0', '#a6da95', '#f0a6ca', '#93c5fd'
];
const bandColor = (i) => BAND_COLORS[i % BAND_COLORS.length];

const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
const fmtFreq = (f) => f >= 1000
  ? `${(f / 1000).toFixed(f < 10000 ? 2 : 1)} kHz`
  : `${Math.round(f)} Hz`;
const fmtGain = (g) => `${g > 0 ? '+' : ''}${g.toFixed(1)} dB`;

// ── REW import ─────────────────────────────────────────
const showImportModal = ref(false);
const importText = ref('');
const importFileInput = ref(null);

const REW_FILTER_RE = /Filter\s+\d+:\s+ON\s+PK\s+Fc\s+([\d.]+)\s*(k?)Hz\s+Gain\s+([-\d.]+)\s+dB\s+Q\s+([\d.]+)/i;

function parseREW(text) {
  const points = [];
  for (const raw of text.split('\n')) {
    // REW inserts thousands separators in some locales ("Fc 1,063.5 Hz").
    // Only strip commas that sit between a digit and a 3-digit group so
    // decimal-comma values are not mangled.
    const line = raw.replace(/(\d),(?=\d{3})/g, '$1');
    const match = line.match(REW_FILTER_RE);
    if (!match) continue;
    points.push({
      freq: clamp(parseFloat(match[1]) * (match[2] ? 1000 : 1), MIN_FREQ, MAX_FREQ),
      gain: clamp(parseFloat(match[3]), -MAX_GAIN, MAX_GAIN),
      q: clamp(parseFloat(match[4]), MIN_Q, MAX_Q)
    });
  }
  return points.slice(0, MAX_POINTS);
}

const importParsedPoints = computed(() => parseREW(importText.value));

const openImport = () => {
  importText.value = '';
  showImportModal.value = true;
};

const onImportFile = async (event) => {
  const file = event.target.files[0];
  if (file) {
    importText.value = await file.text();
  }
  event.target.value = '';
};

const handleImport = () => {
  const newPoints = importParsedPoints.value;
  if (newPoints.length === 0) {
    // Leave the modal open; the parse note explains why nothing happened.
    return;
  }
  localEqPoints.length = 0;
  newPoints.forEach(point => localEqPoints.push(point));
  selectedPoint.value = 0;
  requestUpdate(true);
  showImportModal.value = false;
  importText.value = '';
};

// ── Fullscreen state ───────────────────────────────────
const isFullscreen = ref(false);

// ── Component dimensions ───────────────────────────────
const eqContainer = ref(null);
const svgEl = ref(null);
const chipRail = ref(null);
const width = ref(800);
const height = ref(400);

let resizeObserver = null;

const updateDimensions = () => {
  if (eqContainer.value) {
    const containerWidth = eqContainer.value.clientWidth;
    if (containerWidth > 0) {
      width.value = containerWidth;
    }
    const containerHeight = eqContainer.value.clientHeight;
    if (containerHeight > 0) {
      height.value = containerHeight;
    }
  }
};

// Local copy of EQ points
const localEqPoints = reactive([]);

// Initialize local points from props
function initializePoints() {
  localEqPoints.length = 0; // Clear existing points
  if (props.peqPoints && props.peqPoints.length > 0) {
    props.peqPoints.forEach(point => {
      localEqPoints.push({
        freq: point.freq,
        gain: point.gain,
        q: point.q || 1.0
      });
    });
  }

  // Ensure we have at least one point
  if (localEqPoints.length === 0) {
    localEqPoints.push({ freq: 1000, gain: 0, q: 1 });
  }
};

// Watch for prop changes
const isInteracting = ref(false);
let interactionEndTimeout = null;

watch(() => props.peqPoints, () => {
  if (isInteracting.value) {
    return; // Ignore updates while user is interacting
  }
  initializePoints();
}, { immediate: true, deep: true });

const selectedPoint = ref(0);

// Keep the selected band's chip visible in the rail
watch(selectedPoint, async () => {
  await nextTick();
  const chip = chipRail.value?.children[selectedPoint.value];
  if (chip) {
    chip.scrollIntoView({ behavior: 'smooth', inline: 'nearest', block: 'nearest' });
  }
});

// Throttling and request queuing
let throttleTimeout = null;
let pendingRequest = null; // Promise of the single in-flight per-point PUT
let trailingCall = false; // To track if a call was made during the throttle period
let trailingFullUpdate = false; // Whether the deferred call must be a full-set update
let trailingTimeout = null; // Deferred trailing send scheduled after an in-flight PUT
let isUnmounted = false; // Blocks API traffic scheduled to run after navigation
const THROTTLE_DELAY = 100; // milliseconds

const sendUpdateToAPI = async () => {
  // The full set supersedes any queued per-point send...
  trailingCall = false;
  trailingFullUpdate = false;
  // ...and must not race an in-flight one: wait it out so a straggling
  // point PUT can't land on the backend after the full save.
  if (pendingRequest) {
    try { await pendingRequest; } catch (e) { /* already logged by the sender */ }
  }
  if (isUnmounted) return;

  const pointsToEmit = localEqPoints.map((point, index) => ({
    id: index,
    freq: point.freq,
    gain: point.gain,
    q: point.q
  }));

  // Single save path: the parent listens for `change` and PUTs the full set.
  emit('change', pointsToEmit);
};

const requestUpdate = (fullUpdate = false) => {
  if (isUnmounted) return;

  // Set interaction flag and timeout to clear it
  isInteracting.value = true;
  if (interactionEndTimeout) {
    clearTimeout(interactionEndTimeout);
  }
  interactionEndTimeout = setTimeout(() => {
    isInteracting.value = false;
  }, 500); // Wait for a bit after interaction stops

  if (throttleTimeout) {
    trailingCall = true; // Mark that a call is waiting
    if (fullUpdate) trailingFullUpdate = true; // Don't downgrade a full-set update
    return;
  }

  // Immediately send the first update
  if (fullUpdate) {
    sendUpdateToAPI();
  } else {
    sendPointUpdateToAPI();
  }

  // Set a throttle timeout
  throttleTimeout = setTimeout(() => {
    throttleTimeout = null; // Clear the timeout
    // If there was a trailing call during the throttle period, trigger it now
    if (trailingCall) {
      trailingCall = false;
      const full = trailingFullUpdate;
      trailingFullUpdate = false;
      requestUpdate(full);
    }
  }, THROTTLE_DELAY);
};

const sendPointUpdateToAPI = async () => {
  if (selectedPoint.value === null) return;

  const point = localEqPoints[selectedPoint.value];
  if (!point) return;

  // Serialize point PUTs: never two in flight at once. Queue a trailing
  // send instead so the latest values still reach the backend.
  if (pendingRequest) {
    trailingCall = true;
    return;
  }

  const pointToEmit = {
    id: selectedPoint.value,
    freq: point.freq,
    gain: point.gain,
    q: point.q
  };

  try {
    pendingRequest = VybesAPI.updateEqPoint(props.presetName, pointToEmit);
    await pendingRequest;
  } catch (error) {
    console.error('Failed to update EQ point:', error);
  } finally {
    pendingRequest = null;
    if (trailingCall && !isUnmounted) {
      trailingCall = false;
      const full = trailingFullUpdate;
      trailingFullUpdate = false;
      // Small delay to prevent immediate re-triggering on a fast API
      trailingTimeout = setTimeout(() => requestUpdate(full), 0);
    }
  }
};

// Frequency conversion (logarithmic scale)
const frequencyToX = (freq) => {
  const minFreq = Math.log10(MIN_FREQ);
  const maxFreq = Math.log10(MAX_FREQ);
  const logFreq = Math.log10(freq);
  return ((logFreq - minFreq) / (maxFreq - minFreq)) * width.value;
};

const xToFrequency = (x) => {
  const minFreq = Math.log10(MIN_FREQ);
  const maxFreq = Math.log10(MAX_FREQ);
  const ratio = x / width.value;
  return Math.pow(10, minFreq + ratio * (maxFreq - minFreq));
};

// Gain conversion (linear scale)
const gainToY = (gain) => {
  return height.value - ((gain + MAX_GAIN) / (2 * MAX_GAIN)) * height.value;
};

const yToGain = (y) => {
  const ratio = (height.value - y) / height.value;
  return -MAX_GAIN + ratio * 2 * MAX_GAIN;
};

// Grid lines
const FREQ_MAJORS = [50, 100, 200, 500, 1000, 2000, 5000, 10000];

const freqMajorLines = computed(() => FREQ_MAJORS.map(freq => ({
  value: freq,
  x: frequencyToX(freq),
  label: freq >= 1000 ? `${freq / 1000}k` : `${freq}`
})));

const freqMinorLines = computed(() => {
  const lines = [];
  for (let decade = 10; decade < MAX_FREQ; decade *= 10) {
    for (let m = 2; m < 10; m++) {
      const freq = decade * m;
      if (freq <= MIN_FREQ || freq >= MAX_FREQ || FREQ_MAJORS.includes(freq)) continue;
      lines.push({ value: freq, x: frequencyToX(freq) });
    }
  }
  return lines;
});

const gainGridLines = computed(() => {
  const gains = [-15, -10, -5, 0, 5, 10, 15];
  return gains.map(gain => ({
    value: gain,
    y: gainToY(gain),
    label: gain > 0 ? `+${gain}` : `${gain}`
  }));
});

// EQ curve calculation
const combinedSamples = computed(() => {
  const points = [];
  const steps = 220;
  for (let i = 0; i <= steps; i++) {
    const x = (i / steps) * width.value;
    const freq = xToFrequency(x);
    let totalGain = 0;
    localEqPoints.forEach(point => {
      totalGain += calculateBellFilter(freq, point.freq, point.gain, point.q);
    });
    points.push(`${x.toFixed(1)},${gainToY(totalGain).toFixed(1)}`);
  }
  return points;
});

const combinedPath = computed(() => `M ${combinedSamples.value.join(' L ')}`);

const combinedFillPath = computed(() => {
  const zeroY = gainToY(0).toFixed(1);
  return `${combinedPath.value} L ${width.value},${zeroY} L 0,${zeroY} Z`;
});

const bandPaths = computed(() => localEqPoints.map(point => {
  const samples = [];
  const steps = 120;
  for (let i = 0; i <= steps; i++) {
    const x = (i / steps) * width.value;
    const gain = calculateBellFilter(xToFrequency(x), point.freq, point.gain, point.q);
    samples.push(`${x.toFixed(1)},${gainToY(gain).toFixed(1)}`);
  }
  return `M ${samples.join(' L ')}`;
}));

// Exact bell magnitude shared with the analyzer's auto-EQ (see eq-math.js).
const calculateBellFilter = peakingBellDb;

// Point management
const addPoint = () => {
  addPointAt(1000, 0);
};

const addPointAt = (freq, gain) => {
  if (localEqPoints.length >= MAX_POINTS) return;

  localEqPoints.push({
    freq: clamp(freq, MIN_FREQ, MAX_FREQ),
    gain: clamp(gain, -MAX_GAIN, MAX_GAIN),
    q: 1
  });
  selectedPoint.value = localEqPoints.length - 1;
  requestUpdate(true);
};

const removePoint = async (index) => {
  // If it's the last point, just zero it out instead of removing it.
  if (localEqPoints.length <= 1) {
    if (localEqPoints[0]) {
      localEqPoints[0].gain = 0;
      requestUpdate(true);
    }
    return;
  }

  // Remove the point from the local array
  localEqPoints.splice(index, 1);

  // Adjust the selected point if necessary
  if (selectedPoint.value >= localEqPoints.length) {
    selectedPoint.value = selectedPoint.value - 1;
  }

  // Send the updated list of points to the API.
  // The backend will handle disabling the unused filters.
  requestUpdate(true);
};

// ── Pointer interaction ────────────────────────────────
// All active pointers are tracked so a second finger upgrades the gesture
// to a pinch (Q control) instead of fighting the drag.
const pointers = new Map();
let dragInfo = null;
let pinchInfo = null;
let lastTap = { time: 0, index: -1 };
let readoutHideTimer = null;

const readout = reactive({ show: false, x: 0, y: 0, freq: 1000, gain: 0, q: 1 });

const svgPos = (event) => {
  const rect = svgEl.value.getBoundingClientRect();
  return { x: event.clientX - rect.left, y: event.clientY - rect.top };
};

const pinchDistance = () => {
  const [p1, p2] = [...pointers.values()];
  return Math.hypot(p1.x - p2.x, p1.y - p2.y);
};

const showReadout = (x, y) => {
  const point = localEqPoints[selectedPoint.value];
  if (!point) return;
  readout.freq = point.freq;
  readout.gain = point.gain;
  readout.q = point.q;
  readout.x = clamp(x, 50, width.value - 50);
  readout.y = Math.max(44, y);
  readout.show = true;
  clearTimeout(readoutHideTimer);
};

const hideReadout = (delay = 0) => {
  clearTimeout(readoutHideTimer);
  readoutHideTimer = setTimeout(() => { readout.show = false; }, delay);
};

const onPointerDown = (event) => {
  const pos = svgPos(event);
  pointers.set(event.pointerId, pos);
  try {
    svgEl.value.setPointerCapture(event.pointerId);
  } catch (e) {
    // A pointer can disappear between the event and the capture call
    // (and synthetic events are never capturable) — the gesture still
    // works, it just loses tracking if the finger leaves the SVG.
  }
  event.preventDefault();

  if (pointers.size === 2 && localEqPoints[selectedPoint.value]) {
    // Second finger down: abandon any drag/tap and pinch the selected
    // band's Q instead.
    dragInfo = null;
    lastTap.time = 0; // pinch fingers must not read as a double-tap
    pinchInfo = {
      startDistance: Math.max(10, pinchDistance()),
      startQ: localEqPoints[selectedPoint.value].q
    };
    const point = localEqPoints[selectedPoint.value];
    showReadout(frequencyToX(point.freq), gainToY(point.gain));
    return;
  }
  if (pointers.size > 2) return;

  const nodeEl = event.target.closest('.eq-node');
  if (nodeEl) {
    const index = +nodeEl.dataset.index;
    const now = performance.now();
    if (now - lastTap.time < 300 && lastTap.index === index) {
      removePoint(index);
      lastTap.time = 0;
      return;
    }
    lastTap = { time: now, index };
    selectedPoint.value = index;
    dragInfo = { index, moved: false };
    showReadout(pos.x, pos.y);
  } else {
    dragInfo = { index: -1, x: pos.x, y: pos.y, moved: false }; // maybe a tap-to-add
  }
};

const onPointerMove = (event) => {
  if (!pointers.has(event.pointerId)) return;
  const pos = svgPos(event);
  pointers.set(event.pointerId, pos);

  if (pinchInfo) {
    if (pointers.size < 2) return;
    const point = localEqPoints[selectedPoint.value];
    if (!point) return;
    const distance = Math.max(10, pinchDistance());
    // Spreading fingers widens the bell (lower Q); the exponent tunes
    // sensitivity so a comfortable pinch spans a useful Q range.
    point.q = clamp(pinchInfo.startQ * Math.pow(pinchInfo.startDistance / distance, 1.5), MIN_Q, MAX_Q);
    requestUpdate();
    showReadout(frequencyToX(point.freq), gainToY(point.gain));
    return;
  }

  if (!dragInfo) return;
  if (dragInfo.index < 0) {
    if (Math.hypot(pos.x - dragInfo.x, pos.y - dragInfo.y) > 6) dragInfo.moved = true;
    return;
  }
  dragInfo.moved = true;
  const point = localEqPoints[dragInfo.index];
  point.freq = clamp(xToFrequency(clamp(pos.x, 0, width.value)), MIN_FREQ, MAX_FREQ);
  point.gain = clamp(yToGain(clamp(pos.y, 0, height.value)), -MAX_GAIN, MAX_GAIN);
  requestUpdate();
  showReadout(frequencyToX(point.freq), gainToY(point.gain));
};

const finishPointer = (event, cancelled) => {
  pointers.delete(event.pointerId);

  if (pinchInfo) {
    if (pointers.size < 2) {
      pinchInfo = null;
      hideReadout(600);
      // Reconcile the backend with the final Q even if a throttled
      // per-point update was dropped mid-pinch.
      requestUpdate(true);
    }
    return; // a pinch never falls through to tap-to-add
  }

  if (!dragInfo) return;
  if (dragInfo.index < 0) {
    if (!cancelled && !dragInfo.moved) {
      const pos = svgPos(event);
      addPointAt(xToFrequency(pos.x), yToGain(pos.y));
    }
  } else if (dragInfo.moved) {
    // Send the full set once on release so the backend state is reconciled
    // even if a throttled per-point update was dropped mid-drag.
    requestUpdate(true);
  }
  dragInfo = null;
  hideReadout();
};

const onPointerUp = (event) => finishPointer(event, false);
const onPointerCancel = (event) => finishPointer(event, true);

const onWheel = (event) => {
  const point = localEqPoints[selectedPoint.value];
  if (!point) return;
  point.q = clamp(point.q * Math.exp(-event.deltaY * 0.002), MIN_Q, MAX_Q);
  requestUpdate();
  showReadout(frequencyToX(point.freq), gainToY(point.gain));
  hideReadout(700);
};

// Fullscreen logic
const checkOrientation = () => {
  const isLandscape = window.matchMedia("(orientation: landscape)").matches;
  isFullscreen.value = isLandscape;
  document.body.style.overflow = isLandscape ? 'hidden' : '';

  // Allow time for orientation change and then update dimensions
  nextTick(() => {
    updateDimensions();
  });
};

// Event listeners
onMounted(async () => {
  // Initial dimension update
  await nextTick();
  updateDimensions();

  // Set up resize observer
  if (window.ResizeObserver && eqContainer.value) {
    resizeObserver = new ResizeObserver(updateDimensions);
    resizeObserver.observe(eqContainer.value);
  }

  // Fallback resize listener
  window.addEventListener('resize', updateDimensions);

  // Orientation change listener
  window.addEventListener('resize', checkOrientation);
  checkOrientation(); // Initial check
});

onUnmounted(() => {
  isUnmounted = true; // No API call may fire after navigation

  window.removeEventListener('resize', updateDimensions);
  window.removeEventListener('resize', checkOrientation);

  // Ensure body scroll is unlocked on component unmount
  document.body.style.overflow = '';

  if (resizeObserver) {
    resizeObserver.disconnect();
  }

  if (throttleTimeout) {
    clearTimeout(throttleTimeout);
  }
  clearTimeout(interactionEndTimeout);
  clearTimeout(trailingTimeout);
  clearTimeout(readoutHideTimer);
  pendingRequest = null; // Ensure no pending requests block future operations if component unmounts
});
</script>

<style scoped>
.parametric-eq {
  font-family: 'Segoe UI', system-ui, sans-serif;
  color: #fff;
  width: 100%;
}

.parametric-eq.is-fullscreen {
  position: fixed;
  top: 0;
  left: 0;
  width: 100vw;
  height: 100vh;
  z-index: 9999;
  border-radius: 0;
  background: #12161c;
  /* Keep nodes reachable around the notch on landscape iPhones */
  padding: env(safe-area-inset-top) env(safe-area-inset-right)
           env(safe-area-inset-bottom) env(safe-area-inset-left);
  box-sizing: border-box;
}

.parametric-eq.is-fullscreen .eq-container {
  height: 100%;
  margin-bottom: 0;
  border-radius: 0;
}

.eq-container {
  position: relative;
  width: 100%;
  height: 280px;
  background: linear-gradient(180deg, #161b22 0%, #12161c 100%);
  border-radius: 4px;
  overflow: hidden;
  margin-bottom: 12px;
  min-width: 300px; /* Minimum width for usability */
}

/* Full-bleed on portrait phones: escape every ancestor's card padding */
@media (max-width: 640px) and (orientation: portrait) {
  .parametric-eq:not(.is-fullscreen) .eq-container {
    width: 100vw;
    margin-left: calc(50% - 50vw);
    min-width: 0;
    border-radius: 0;
  }
}

.eq-svg {
  display: block;
  touch-action: none; /* stops Safari hijacking pinch as page zoom */
  cursor: crosshair;
}

.eq-svg .eq-node {
  cursor: grab;
}

.eq-svg .eq-node:active {
  cursor: grabbing;
}

.drag-readout {
  position: absolute;
  pointer-events: none;
  background: rgba(10, 13, 17, 0.92);
  border: 1px solid rgba(148, 168, 196, 0.22);
  border-radius: 6px;
  padding: 5px 9px;
  font-size: 11px;
  line-height: 1.5;
  font-family: ui-monospace, 'SF Mono', Menlo, monospace;
  font-variant-numeric: tabular-nums;
  white-space: nowrap;
  opacity: 0;
  transition: opacity 0.12s;
  transform: translate(-50%, -130%);
}

.drag-readout.show {
  opacity: 1;
}

/* ── Band rail ─────────────────────────────────────── */
.point-selection {
  display: flex;
  gap: 8px;
  overflow-x: auto;
  scrollbar-width: none;
  -webkit-overflow-scrolling: touch;
  scroll-snap-type: x proximity;
  margin-bottom: 12px;
  padding: 2px;
  mask-image: linear-gradient(90deg, transparent, #000 12px, #000 calc(100% - 12px), transparent);
  -webkit-mask-image: linear-gradient(90deg, transparent, #000 12px, #000 calc(100% - 12px), transparent);
}

.point-selection::-webkit-scrollbar {
  display: none;
}

.point-chip {
  flex: 0 0 auto; /* chips scroll, never squish */
  scroll-snap-align: center;
  display: flex;
  align-items: center;
  gap: 7px;
  background: #1a2029;
  border: 1px solid rgba(148, 168, 196, 0.10);
  border-radius: 999px;
  padding: 7px 13px 7px 10px;
  font-size: 12px;
  font-family: ui-monospace, 'SF Mono', Menlo, monospace;
  font-variant-numeric: tabular-nums;
  color: #8b96a8;
  cursor: pointer;
  transition: border-color 0.15s, color 0.15s, background 0.15s;
  -webkit-tap-highlight-color: transparent;
}

.point-chip.active {
  color: #d7dee9;
  background: #222a35;
  border-color: var(--chip-color, rgba(148, 168, 196, 0.22));
}

.point-chip.chip-ghost {
  border-style: dashed;
  font-family: inherit;
}

.point-chip:focus-visible {
  outline: 2px solid #f5c04e;
  outline-offset: 2px;
}

.chip-dot {
  width: 9px;
  height: 9px;
  border-radius: 50%;
  flex: none;
}

/* ── Selected band controls ────────────────────────── */
.eq-controls {
  background: #1a2029;
  border: 1px solid rgba(148, 168, 196, 0.10);
  padding: 16px;
  border-radius: 10px;
}

.eq-controls-head {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 12px;
}

.eq-controls-head .chip-dot {
  width: 10px;
  height: 10px;
}

.eq-controls h4 {
  margin: 0;
  font-size: 14px;
}

.delete-band-btn {
  margin-left: auto;
  background: none;
  border: 1px solid rgba(148, 168, 196, 0.22);
  color: #8b96a8;
  border-radius: 6px;
  font-size: 11px;
  padding: 4px 10px;
  cursor: pointer;
}

.delete-band-btn:hover {
  color: #ff8a80;
  border-color: rgba(255, 138, 128, 0.4);
}

.import-file-btn {
  background: none;
  border: 1px solid rgba(148, 168, 196, 0.22);
  color: #8b96a8;
  border-radius: 6px;
  font-size: 12px;
  padding: 5px 12px;
  cursor: pointer;
}

.import-file-btn:hover {
  color: #f5c04e;
  border-color: #f5c04e;
}

.control-group {
  margin-bottom: 15px;
}

.control-group:last-child {
  margin-bottom: 0;
}
</style>
