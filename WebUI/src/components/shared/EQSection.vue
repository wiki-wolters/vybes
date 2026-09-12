<template>
  <div class="space-y-4">
    <!-- Volume anchors: which level each curve is tuned at -->
    <div class="anchor-row">
      <div class="min-w-0">
        <p class="anchor-title">Reference volume {{ store.referenceVolume }}%</p>
        <p class="anchor-note">The level this curve is tuned at.</p>
      </div>
      <button
        type="button"
        class="btn-secondary flex-none"
        :disabled="store.referenceVolume === currentVolume"
        @click="store.setEqAnchors(currentVolume, store.loudVolume)"
      >
        Set to current volume
      </button>
    </div>

    <div class="anchor-row">
      <div class="min-w-0">
        <p class="anchor-title">
          Loud anchor<template v-if="store.hasLoudAnchor">: {{ store.loudVolume }}%</template>
          <template v-else>: none</template>
        </p>
        <p class="anchor-note">
          {{ store.hasLoudAnchor
            ? 'Above the reference the curve moves toward this one, then holds.'
            : 'Add one to make the curve follow the volume upwards.' }}
        </p>
      </div>
      <div class="flex gap-2 flex-none">
        <template v-if="store.hasLoudAnchor">
          <button
            type="button"
            class="btn-secondary"
            :disabled="!canAnchorLoudHere"
            @click="store.setEqAnchors(store.referenceVolume, currentVolume)"
          >
            Move to current
          </button>
          <button type="button" class="btn-secondary" @click="removeLoudAnchor">Remove</button>
        </template>
        <button
          v-else
          type="button"
          class="btn-secondary"
          :disabled="!canAnchorLoudHere"
          @click="addLoudAnchor"
        >
          Add loud anchor at current volume
        </button>
      </div>
    </div>
    <p v-if="!canAnchorLoudHere" class="anchor-note">
      Turn the volume above {{ store.referenceVolume }}% to place the loud anchor.
    </p>

    <!-- Which anchor the graph below edits -->
    <div v-if="store.hasLoudAnchor" class="segmented" role="tablist" aria-label="Editing">
      <span class="segmented-label">Editing</span>
      <button
        v-for="option in EDIT_MODES"
        :key="option.id"
        type="button"
        role="tab"
        :aria-selected="editing === option.id"
        :class="['segment', editing === option.id ? 'segment-active' : '']"
        @click="editing = option.id"
      >{{ option.label }}</button>
    </div>

    <ParametricEQ
      :peq-points="editedPoints"
      :preset-name="presetName"
      :eq-type="eqType"
      :gains-only="editing === 'loud'"
      :overlay-db="showNowCurve ? nowCurveDb : null"
      :overlay-label="showNowCurve ? `Now at ${currentVolume}%` : ''"
      @change="onChange"
      class="min-h-[400px] h-auto"
    />

    <div class="anchor-row">
      <div class="min-w-0">
        <p class="anchor-title">Loudness compensation below reference</p>
        <p class="anchor-note">
          Keeps the bass in proportion as the volume drops, following the
          equal-loudness contours.
        </p>
      </div>
      <ToggleSwitch
        :model-value="store.loudness"
        aria-label="Loudness compensation below reference"
        class="flex-none"
        @update:modelValue="store.setLoudness($event)"
      />
    </div>
  </div>
</template>

<script setup>
/*
 * The preset's shared input EQ, with its dynamic-EQ anchors
 * (docs/DYNAMIC_EQ.md). One band list, two sets of gains: the reference
 * anchor is what the preset is tuned at, the optional loud anchor is what it
 * should become when turned up. Below the reference an automatic
 * ISO 226-derived compensation takes over, with nothing to configure.
 */
import { computed, ref, watch } from 'vue';
import ParametricEQ from '../ParametricEQ.vue';
import ToggleSwitch from './ToggleSwitch.vue';
import { usePresetStore } from '../../stores/preset.js';
import {
  peakingBellDb, volumePctToDb, loudnessCompensationRelDb, dynamicEqGains,
} from '../../eq-math.js';

defineProps({
  presetName: {
    type: String,
    required: true
  },
  eqType: {
    type: String,
    default: 'pref'
  }
});

const store = usePresetStore();

const EDIT_MODES = [
  { id: 'reference', label: 'Reference' },
  { id: 'loud', label: 'Loud' },
];
const editing = ref('reference');

// Removing the loud anchor leaves nothing to edit in Loud mode
watch(() => store.hasLoudAnchor, (has) => {
  if (!has) editing.value = 'reference';
});

const currentVolume = computed(() => store.preset?.volume ?? 0);
// A loud anchor at or below the reference has no range to interpolate across
const canAnchorLoudHere = computed(() => currentVolume.value > store.referenceVolume);

const referenceGains = computed(() => store.inputEqPoints.map((p) => p.gain));

/** The bands the graph edits: the same frequencies and Qs either way */
const editedPoints = computed(() => {
  if (editing.value !== 'loud') return store.inputEqPoints;
  return store.inputEqPoints.map((p, i) => ({
    freq: p.freq,
    gain: store.loudGains[i] ?? 0,
    q: p.q,
  }));
});

function onChange(points) {
  if (editing.value === 'loud') {
    store.saveLoudGains(points.map((p) => p.gain));
  } else {
    store.saveInputEq(points);
  }
}

// ── What is playing right now ──────────────────────────
const volumeDb = computed(() => volumePctToDb(currentVolume.value));
const referenceDb = computed(() => volumePctToDb(store.referenceVolume));
const loudDb = computed(() => volumePctToDb(store.loudVolume));
// How far below the reference anchor the volume sits, in dB
const dropDb = computed(() => Math.max(0, referenceDb.value - volumeDb.value));

const effectiveGains = computed(() => dynamicEqGains(
  referenceGains.value, store.loudGains,
  referenceDb.value, loudDb.value, store.hasLoudAnchor, volumeDb.value,
));

const nowCurveDb = (freq) => {
  let total = store.loudness ? loudnessCompensationRelDb(freq, dropDb.value) : 0;
  store.inputEqPoints.forEach((point, i) => {
    total += peakingBellDb(freq, point.freq, effectiveGains.value[i] ?? 0, point.q);
  });
  return total;
};

// Drawing a second curve on top of an identical one is just noise
const showNowCurve = computed(() => {
  if (!store.preset) return false;
  if (store.loudness && dropDb.value > 0.1) return true;
  const edited = editedPoints.value.map((p) => p.gain);
  return edited.some((gain, i) => Math.abs(gain - (effectiveGains.value[i] ?? 0)) > 0.05);
});

// ── Anchor actions ─────────────────────────────────────

/*
 * A new loud anchor starts as a copy of the reference curve, so placing it
 * changes nothing audibly until its gains are edited. The gains go first and
 * the anchor second, the same order the device pushes them to the Teensy.
 */
async function addLoudAnchor() {
  await store.saveLoudGains(referenceGains.value);
  await store.setEqAnchors(store.referenceVolume, currentVolume.value);
  editing.value = 'loud';
}

async function removeLoudAnchor() {
  editing.value = 'reference';
  await store.clearLoudAnchor();
}
</script>

<style scoped>
@reference "../../style.css";

.anchor-row {
  @apply flex items-center justify-between gap-3 rounded-md bg-vybes-dark-element/40 p-3;
}

.anchor-title {
  @apply font-medium text-vybes-text-primary;
}

.anchor-note {
  @apply text-xs text-vybes-text-secondary mt-0.5;
}

.segmented {
  @apply flex items-center gap-1;
}

.segmented-label {
  @apply text-sm text-vybes-text-secondary mr-2;
}

.segment {
  @apply px-3 py-1.5 rounded-md text-sm cursor-pointer
         bg-vybes-dark-card border border-vybes-border text-vybes-text-secondary;
}

/* Blue, like the editor's tabs: this picks a view, it is not a live state */
.segment-active {
  @apply bg-vybes-dark-input text-vybes-text-primary border-vybes-primary;
}
</style>
