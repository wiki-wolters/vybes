<template>
  <CardSection title="Dynamics">
    <template v-if="dyn">
      <!-- Mode chips: the whole simple tier is "pick a mode, set strength" -->
      <div class="flex flex-wrap items-center gap-2 mb-3">
        <button
          class="px-3 py-1 rounded-full text-xs border transition-colors"
          :class="!dyn.enabled
            ? 'bg-vybes-primary text-white border-transparent'
            : 'border-vybes-border text-vybes-text-secondary hover:text-vybes-text-primary'"
          @click="selectOff"
        >
          Off
        </button>
        <button
          v-for="mode in modes"
          :key="mode.id"
          class="px-3 py-1 rounded-full text-xs border transition-colors"
          :class="activeModeId === mode.id
            ? 'bg-vybes-primary text-white border-transparent'
            : 'border-vybes-border text-vybes-text-secondary hover:text-vybes-text-primary'"
          @click="selectMode(mode)"
        >
          {{ mode.label }}
        </button>
        <span
          v-if="dyn.enabled && !activeModeId"
          class="px-3 py-1 rounded-full text-xs bg-black/30 border border-vybes-border text-vybes-accent"
        >
          Custom
        </span>
      </div>
      <p class="text-xs text-vybes-text-secondary mb-4">{{ hintText }}</p>

      <template v-if="dyn.enabled">
        <RangeSlider
          label="Strength"
          :min="0"
          :max="100"
          :step="5"
          unit="%"
          :decimals="0"
          :model-value="dyn.strength"
          @update:model-value="(v) => setParam(() => (dyn.strength = Number(v)))"
        />

        <!-- Live gain reduction -->
        <div class="mt-4 flex items-center gap-4">
          <span class="text-xs text-vybes-text-secondary shrink-0">Reduction</span>
          <div class="flex-1 grid grid-cols-3 gap-3">
            <div v-for="(name, i) in bandNames" :key="name">
              <div class="flex justify-between text-[11px] text-vybes-text-secondary mb-0.5">
                <span>{{ name }}</span>
                <span class="tabular-nums">−{{ grDb[i].toFixed(1) }}</span>
              </div>
              <div class="h-1.5 rounded bg-black/40 overflow-hidden">
                <div
                  class="h-full rounded transition-[width] duration-100 ease-linear"
                  :style="{ width: meterWidth(i), background: BAND_COLORS[i] }"
                />
              </div>
            </div>
          </div>
        </div>

        <!-- Advanced tier -->
        <div class="mt-5">
          <CollapsibleSection title="Advanced" :toggleable="false" :start-expanded="false" :animate="false">
            <!-- Band split strip: drag the dividers -->
            <div class="flex items-baseline justify-between mb-2">
              <p class="text-xs text-vybes-text-secondary">Band split — drag the dividers</p>
              <p class="text-xs text-vybes-text-secondary tabular-nums">
                {{ fmtHz(dyn.xoverLow) }} · {{ fmtHz(dyn.xoverHigh) }}
              </p>
            </div>
            <div ref="splitContainer" class="w-full rounded bg-black/30 overflow-hidden mb-1">
              <svg
                :width="splitWidth"
                :height="SPLIT_HEIGHT"
                class="block"
                style="touch-action: none"
                @pointerdown="onSplitPointerDown"
                @pointermove="onSplitPointerMove"
                @pointerup="onSplitPointerUp"
                @pointercancel="onSplitPointerUp"
              >
                <rect v-for="(band, i) in splitBands" :key="'r' + i"
                  :x="band.x0" y="0" :width="band.x1 - band.x0" :height="SPLIT_HEIGHT"
                  :fill="BAND_COLORS[i]" opacity="0.16" />
                <text v-for="(band, i) in splitBands" :key="'t' + i"
                  :x="(band.x0 + band.x1) / 2" :y="SPLIT_HEIGHT / 2 + 4"
                  text-anchor="middle" font-size="11" :fill="BAND_COLORS[i]">{{ bandNames[i] }}</text>
                <g v-for="(x, i) in dividerXs" :key="'d' + i" class="cursor-ew-resize">
                  <rect :x="x - 10" y="0" width="20" :height="SPLIT_HEIGHT" fill="transparent" />
                  <line :x1="x" y1="0" :x2="x" :y2="SPLIT_HEIGHT"
                    stroke="#fff" :opacity="dragging === i ? 0.9 : 0.45" stroke-width="2" />
                </g>
              </svg>
            </div>
            <div class="flex justify-between text-[10px] text-vybes-text-secondary mb-4">
              <span>20 Hz</span><span>20 kHz</span>
            </div>

            <!-- Per-band parameters -->
            <div class="grid sm:grid-cols-3 gap-x-5 gap-y-4">
              <div v-for="(band, i) in dyn.bands" :key="'band' + i"
                class="rounded bg-black/20 p-3"
                :class="{ 'opacity-60': band.bypass }"
              >
                <div class="flex items-center justify-between mb-2">
                  <span class="text-xs font-medium" :style="{ color: BAND_COLORS[i] }">{{ bandNames[i] }}</span>
                  <span class="flex gap-1.5">
                    <button
                      class="px-2 py-0.5 rounded text-[11px] border"
                      :class="soloBand === i
                        ? 'bg-vybes-accent text-black border-transparent'
                        : 'border-vybes-border text-vybes-text-secondary'"
                      @click="toggleSolo(i)"
                    >
                      Solo
                    </button>
                    <button
                      class="px-2 py-0.5 rounded text-[11px] border"
                      :class="band.bypass
                        ? 'bg-vybes-border text-vybes-text-primary border-transparent'
                        : 'border-vybes-border text-vybes-text-secondary'"
                      @click="setParam(() => (band.bypass = !band.bypass))"
                    >
                      Bypass
                    </button>
                  </span>
                </div>
                <RangeSlider label="Threshold" :min="-60" :max="0" :step="0.5" unit="dB" :decimals="1"
                  :model-value="band.threshold"
                  @update:model-value="(v) => setParam(() => (band.threshold = Number(v)))" />
                <RangeSlider label="Ratio" :min="1" :max="20" :step="0.1" unit=":1" :decimals="1"
                  :model-value="band.ratio"
                  @update:model-value="(v) => setParam(() => (band.ratio = Number(v)))" />
                <RangeSlider label="Attack" :min="0.5" :max="500" :step="0.5" unit="ms" :decimals="1" :logarithmic="true"
                  :model-value="band.attack"
                  @update:model-value="(v) => setParam(() => (band.attack = Number(v)))" />
                <RangeSlider label="Release" :min="10" :max="2000" :step="5" unit="ms" :decimals="0" :logarithmic="true"
                  :model-value="band.release"
                  @update:model-value="(v) => setParam(() => (band.release = Number(v)))" />
                <RangeSlider label="Makeup" :min="-12" :max="12" :step="0.5" unit="dB" :decimals="1"
                  :model-value="band.makeup"
                  @update:model-value="(v) => setParam(() => (band.makeup = Number(v)))" />
              </div>
            </div>

            <div class="mt-4">
              <RangeSlider label="Voice priority" :min="0" :max="12" :step="0.5" unit="dB" :decimals="1"
                :model-value="dyn.voicePriority"
                @update:model-value="(v) => setParam(() => (dyn.voicePriority = Number(v)))" />
              <p class="mt-1 text-xs text-vybes-text-secondary">
                While the mid band hears sustained content (voice), bass is allowed up to this
                much extra reduction — explosions duck harder exactly while someone is talking.
              </p>
            </div>
          </CollapsibleSection>
        </div>
      </template>

      <p v-if="saveError" class="mt-3 text-xs text-red-400">{{ saveError }}</p>
    </template>
    <p v-else class="text-xs text-vybes-text-secondary">Waiting for the device…</p>
  </CardSection>
</template>

<script setup>
import { ref, computed, watch, onMounted, onUnmounted } from 'vue';
import apiClient from '../api-client.js';
import CardSection from './shared/CardSection.vue';
import RangeSlider from './shared/RangeSlider.vue';
import CollapsibleSection from './shared/CollapsibleSection.vue';
import {
  DYNAMICS_MODES,
  COMP_BAND_NAMES,
  dynamicsModeById,
  matchesMode,
  decodeGrmFrame,
} from '../dynamics.js';

const SAVE_DEBOUNCE_MS = 350;
const KEEPALIVE_INTERVAL_MS = 2000;
// Suppress echo of our own dynamicsChanged broadcast while edits are fresh
const ECHO_SUPPRESS_MS = 1200;
const METER_FULL_SCALE_DB = 12;
const SPLIT_HEIGHT = 44;

// Matches the analyzer's delta-bar palette: blue bass, green mid, amber treble
const BAND_COLORS = ['#38bdf8', '#4ade80', '#f5c04e'];

const modes = DYNAMICS_MODES;
const bandNames = COMP_BAND_NAMES;

const presetName = ref('');
const dyn = ref(null);
const grDb = ref([0, 0, 0]);
const soloBand = ref(-1);
const saveError = ref('');
const dragging = ref(null); // divider index while dragging, else null

let lastLocalEditAt = 0;
let saveTimer = null;
let keepaliveTimer = null;
let unsubscribeLive = null;

// --- Mode selection ---

const activeModeId = computed(() => {
  if (!dyn.value?.enabled) return null;
  const mode = dynamicsModeById(dyn.value.mode);
  return mode && matchesMode(dyn.value, mode) ? mode.id : null;
});

const hintText = computed(() => {
  if (!dyn.value?.enabled) return 'Compressor bypassed — nothing in the signal path.';
  const mode = dynamicsModeById(activeModeId.value);
  return mode ? mode.hint : 'Custom settings — tuned in the advanced view below.';
});

function selectOff() {
  dyn.value.enabled = false;
  scheduleSave(0);
}

function selectMode(mode) {
  const s = mode.settings;
  dyn.value = {
    ...dyn.value,
    enabled: true,
    mode: mode.id,
    xoverLow: s.xoverLow,
    xoverHigh: s.xoverHigh,
    voicePriority: s.voicePriority,
    bands: s.bands.map((b) => ({ ...b })),
  };
  scheduleSave(0);
}

// Advanced edits go through here: apply the mutation, relabel the block
// (back to a mode if it happens to match one, else custom), save.
function setParam(mutate) {
  mutate();
  const match = modes.find((m) => matchesMode(dyn.value, m));
  dyn.value.mode = match ? match.id : 'custom';
  scheduleSave();
}

// --- Persistence ---

function scheduleSave(delay = SAVE_DEBOUNCE_MS) {
  lastLocalEditAt = Date.now();
  saveError.value = '';
  clearTimeout(saveTimer);
  saveTimer = setTimeout(async () => {
    saveTimer = null;
    if (!presetName.value || !dyn.value) return;
    try {
      await apiClient.savePresetDynamics(presetName.value, dyn.value);
    } catch (err) {
      saveError.value = `Failed to save dynamics: ${err.message}`;
    }
  }, delay);
}

// --- Live updates ---

function onLiveMessage(data) {
  if (!data) return;
  if (data.type === 'grm') {
    const decoded = decodeGrmFrame(data.d);
    if (decoded) grDb.value = decoded;
    return;
  }
  if (data.messageType === 'activePresetChanged' && data.activePresetName) {
    loadPreset();
    return;
  }
  if (
    data.messageType === 'dynamicsChanged' &&
    data.presetName === presetName.value &&
    data.dynamics &&
    // Our own PUT echoes back; don't fight fresh local edits
    Date.now() - lastLocalEditAt > ECHO_SUPPRESS_MS && !saveTimer
  ) {
    dyn.value = data.dynamics;
  }
}

async function loadPreset() {
  try {
    const preset = await apiClient.getCurrentPreset();
    if (preset) {
      presetName.value = preset.name;
      dyn.value = preset.dynamics || null;
    }
  } catch (e) {
    // Device offline: the card shows its waiting state
  }
}

// --- Meters ---

const meterWidth = (i) =>
  `${Math.min(100, (grDb.value[i] / METER_FULL_SCALE_DB) * 100).toFixed(1)}%`;

// --- Band split editor (log-frequency axis, 20Hz-20kHz) ---

const splitContainer = ref(null);
const splitWidth = ref(320);
let resizeObserver = null;

const LOG_LO = Math.log10(20);
const LOG_HI = Math.log10(20000);
const xForFreq = (f) => ((Math.log10(f) - LOG_LO) / (LOG_HI - LOG_LO)) * splitWidth.value;
const freqAtX = (x) => Math.pow(10, LOG_LO + (x / splitWidth.value) * (LOG_HI - LOG_LO));

const dividerXs = computed(() => [xForFreq(dyn.value.xoverLow), xForFreq(dyn.value.xoverHigh)]);

const splitBands = computed(() => {
  const [x1, x2] = dividerXs.value;
  return [
    { x0: 0, x1: x1 },
    { x0: x1, x1: x2 },
    { x0: x2, x1: splitWidth.value },
  ];
});

const fmtHz = (f) => (f >= 1000 ? `${(f / 1000).toFixed(f >= 10000 ? 0 : 1)} kHz` : `${Math.round(f)} Hz`);

// offsetX is relative to the event target (which can be a child rect of
// the svg), so derive the x from clientX against the svg itself.
function svgX(event) {
  return event.clientX - event.currentTarget.getBoundingClientRect().left;
}

function onSplitPointerDown(event) {
  const x = svgX(event);
  const [x1, x2] = dividerXs.value;
  // Grab the nearer divider if the press is within reach of it
  const d = Math.abs(x - x1) <= Math.abs(x - x2) ? 0 : 1;
  if (Math.abs(x - (d === 0 ? x1 : x2)) > 24) return;
  dragging.value = d;
  try {
    event.currentTarget.setPointerCapture(event.pointerId);
  } catch (e) { /* keeps working without capture; pointerup still ends the drag */ }
}

function onSplitPointerMove(event) {
  if (dragging.value === null) return;
  const f = freqAtX(svgX(event));
  // Same bounds the ESP and Teensy enforce, plus a 2x gap between splits
  if (dragging.value === 0) {
    const upper = Math.min(1000, dyn.value.xoverHigh / 2);
    setParam(() => (dyn.value.xoverLow = Math.round(Math.min(upper, Math.max(40, f)))));
  } else {
    const lower = Math.max(2 * dyn.value.xoverLow, 800);
    setParam(() => (dyn.value.xoverHigh = Math.round(Math.min(12000, Math.max(lower, f)))));
  }
}

function onSplitPointerUp() {
  dragging.value = null;
}

// --- Solo (transient audition, never stored) ---

async function toggleSolo(band) {
  const next = soloBand.value === band ? -1 : band;
  soloBand.value = next;
  try {
    await apiClient.setCompSolo(next);
  } catch (e) { /* audition is best-effort */ }
}

// --- Lifecycle ---

onMounted(() => {
  loadPreset();
  unsubscribeLive = apiClient.connectLiveUpdates(onLiveMessage);
  // Meters stream only while someone is watching; the keepalive is cheap,
  // so send it whenever the compressor is on and this card exists.
  keepaliveTimer = setInterval(() => {
    if (dyn.value?.enabled) apiClient.sendLiveMessage('grm:keepalive');
  }, KEEPALIVE_INTERVAL_MS);

});

// The split editor lives inside v-if + collapsible content, so its element
// comes and goes; (re)measure and (re)observe whenever it binds.
const updateWidth = () => {
  if (splitContainer.value?.clientWidth > 0) splitWidth.value = splitContainer.value.clientWidth;
};
watch(splitContainer, (el, prev) => {
  if (prev && resizeObserver) resizeObserver.unobserve(prev);
  if (el) {
    updateWidth();
    if (window.ResizeObserver) {
      if (!resizeObserver) resizeObserver = new ResizeObserver(updateWidth);
      resizeObserver.observe(el);
    }
  }
});

onUnmounted(() => {
  if (unsubscribeLive) unsubscribeLive();
  clearInterval(keepaliveTimer);
  clearTimeout(saveTimer);
  if (resizeObserver) resizeObserver.disconnect();
  if (soloBand.value >= 0) apiClient.setCompSolo(-1);
});
</script>
