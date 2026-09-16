<template>
  <div class="space-y-4">
    <!-- Volume anchors. The chips double as the graph's tab strip: the one
         picked is the curve the graph edits, so the anchors and the choice of
         what to edit cost a single row instead of three -->
    <div>
      <div class="anchor-bar">
        <div class="anchor-tabs" role="tablist" aria-label="Anchor to edit">
          <button
            type="button"
            role="tab"
            :aria-selected="editing === 'reference'"
            :class="['anchor-chip', editing === 'reference' ? 'anchor-chip-active' : '']"
            @click="editing = 'reference'"
          >Reference <span class="tabular-nums">{{ store.referenceVolume }}%</span></button>

          <button
            v-if="store.hasLoudAnchor"
            type="button"
            role="tab"
            :aria-selected="editing === 'loud'"
            :class="['anchor-chip', editing === 'loud' ? 'anchor-chip-active' : '']"
            @click="editing = 'loud'"
          >Loud <span class="tabular-nums">{{ store.loudVolume }}%</span></button>

          <button
            v-else
            type="button"
            class="anchor-chip anchor-chip-add"
            :disabled="!canAnchorLoudHere"
            @click="addLoudAnchor"
          >+ Loud anchor</button>
        </div>

        <!-- Whatever the selected chip can have done to it -->
        <div class="anchor-actions">
          <button
            v-if="editing === 'reference'"
            type="button"
            class="anchor-action"
            :disabled="store.referenceVolume === currentVolume"
            @click="store.setEqAnchors(currentVolume, store.loudVolume)"
          >Set to current</button>
          <template v-else>
            <button
              type="button"
              class="anchor-action"
              :disabled="!canAnchorLoudHere"
              @click="store.setEqAnchors(store.referenceVolume, currentVolume)"
            >Move to current</button>
            <button type="button" class="anchor-action" @click="removeLoudAnchor">Remove</button>
          </template>
        </div>
      </div>
      <p class="anchor-hint">{{ anchorHint }}</p>
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

const editing = ref('reference');

// Removing the loud anchor leaves nothing to edit in Loud mode
watch(() => store.hasLoudAnchor, (has) => {
  if (!has) editing.value = 'reference';
});

const currentVolume = computed(() => store.preset?.volume ?? 0);
// A loud anchor at or below the reference has no range to interpolate across
const canAnchorLoudHere = computed(() => currentVolume.value > store.referenceVolume);

/*
 * One line under the chips, carrying whichever explanation is useful in the
 * state the anchors are actually in. The gains-only rule for the loud curve is
 * ParametricEQ's own hint, so it is not repeated here.
 */
const anchorHint = computed(() => {
  if (store.hasLoudAnchor) {
    return `Between ${store.referenceVolume}% and ${store.loudVolume}% the curve moves `
      + 'from Reference to Loud, then holds.';
  }
  return canAnchorLoudHere.value
    ? `The curve is tuned at ${store.referenceVolume}%. Add a loud anchor to make it `
      + 'follow the volume upwards.'
    : `The curve is tuned at ${store.referenceVolume}%. Turn the volume above that to `
      + 'add a loud anchor for it to follow upwards.';
});

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

.anchor-bar {
  @apply flex flex-wrap items-center justify-between gap-x-4 gap-y-2;
}

.anchor-hint {
  @apply text-xs text-vybes-text-secondary mt-2;
}

.anchor-tabs {
  @apply flex flex-wrap items-center gap-2;
}

/* Same chip language as the channel rail and the EQ band rail */
.anchor-chip {
  @apply flex-none rounded-full px-3 py-1.5 text-xs whitespace-nowrap cursor-pointer
         bg-vybes-dark-card border border-vybes-border text-vybes-text-secondary
         disabled:opacity-50 disabled:cursor-not-allowed;
}

/* Blue, like the editor's tabs: this picks a view, it is not a live state */
.anchor-chip-active {
  @apply bg-vybes-dark-input text-vybes-text-primary border-vybes-primary;
}

/* Dashed: a slot for an anchor rather than one that exists */
.anchor-chip-add {
  @apply border-dashed;
}

.anchor-actions {
  @apply flex flex-wrap items-center gap-2;
}

.anchor-action {
  @apply rounded-md px-3 py-2 text-xs whitespace-nowrap cursor-pointer transition-colors
         bg-vybes-dark-element hover:bg-vybes-dark-input
         text-vybes-text-secondary hover:text-vybes-text-primary
         disabled:opacity-50 disabled:cursor-not-allowed disabled:hover:bg-vybes-dark-element
         disabled:hover:text-vybes-text-secondary;
}
</style>
