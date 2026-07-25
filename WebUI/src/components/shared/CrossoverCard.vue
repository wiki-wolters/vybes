<template>
  <CollapsibleSection title="Crossovers" :toggleable="false" :animate="animate">
    <div class="space-y-4">
      <div
        v-for="point in store.crossovers"
        :key="point.id"
        class="rounded-md bg-vybes-dark-element/40 p-3"
      >
        <!-- Unlocked point (e.g. sub crossover): live slider + bypass toggle -->
        <template v-if="!point.locked">
          <div class="flex items-center justify-between mb-2">
            <span class="font-medium text-vybes-text-primary">{{ pointTitle(point) }}</span>
            <ToggleSwitch
              :model-value="store.isCrossoverEnabled(point.id)"
              @update:modelValue="store.setCrossoverEnabled(point.id, $event)"
            />
          </div>
          <div :class="{ 'opacity-50': !store.isCrossoverEnabled(point.id) }">
            <RangeSlider
              :model-value="Number(point.freq)"
              label="Frequency"
              :min="point.min"
              :max="point.max"
              :step="5"
              unit="Hz"
              :decimals="0"
              @update:modelValue="store.setCrossoverFreq(point.id, $event)"
            />
          </div>
        </template>

        <!-- Locked point: view-only, edited via the guarded modal flow -->
        <template v-else>
          <div class="flex items-center justify-between gap-3">
            <div class="flex items-center gap-2 min-w-0">
              <svg class="w-4 h-4 text-vybes-text-secondary flex-none" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 15v2m-6 4h12a2 2 0 002-2v-6a2 2 0 00-2-2H6a2 2 0 00-2 2v6a2 2 0 002 2zm10-10V7a4 4 0 00-8 0v4h8z"></path>
              </svg>
              <span class="font-medium text-vybes-text-primary truncate">{{ pointTitle(point) }}</span>
            </div>
            <div class="flex items-center gap-3 flex-none">
              <span class="text-vybes-text-primary font-semibold">{{ point.freq }} Hz</span>
              <span class="text-xs text-vybes-text-secondary">{{ point.type }}</span>
              <button class="btn-secondary" @click="openEdit(point)">Edit</button>
            </div>
          </div>
          <p class="text-xs text-vybes-text-secondary mt-1">
            Feeds {{ affectedSummary(point.id) }}
          </p>
        </template>
      </div>

      <p v-if="store.crossovers.length === 0" class="text-sm text-vybes-text-secondary">
        This preset has no shared crossover points.
      </p>
    </div>
  </CollapsibleSection>

  <LockedValueModal
    v-if="editingPoint"
    v-model="showEditModal"
    :title="pointTitle(editingPoint)"
    :value="editingPoint.freq"
    unit="Hz"
    :min="editingPoint.min"
    :max="editingPoint.max"
    :affected="affectedList(editingPoint.id)"
    :error="modalError"
    :busy="modalBusy"
    @confirm="applyLockedEdit"
  />
</template>

<script setup>
import { ref } from 'vue';
import CollapsibleSection from './CollapsibleSection.vue';
import RangeSlider from './RangeSlider.vue';
import ToggleSwitch from './ToggleSwitch.vue';
import LockedValueModal from './LockedValueModal.vue';
import { usePresetStore } from '../../stores/preset.js';

defineProps({
  animate: { type: Boolean, default: false },
});

const store = usePresetStore();

const showEditModal = ref(false);
const editingPoint = ref(null);
const modalError = ref('');
const modalBusy = ref(false);

const POINT_TITLES = {
  sub_xo: 'Subwoofer Crossover',
  mid_xo: 'Mid Crossover',
  twt_xo: 'Tweeter Crossover',
};
const pointTitle = (point) => POINT_TITLES[point.id] || `Crossover ${point.id}`;

/** "High-pass on L Mid", "Low-pass on L Low", ... for the confirm step */
function affectedList(id) {
  const items = [];
  for (const output of store.outputsReferencing(id)) {
    if (output.hp.xover === id) items.push(`High-pass on ${output.label}`);
    if (output.lp.xover === id) items.push(`Low-pass on ${output.label}`);
  }
  return items;
}

function affectedSummary(id) {
  const labels = store.outputsReferencing(id).map((o) => o.label);
  return labels.length ? labels.join(', ') : 'no outputs';
}

function openEdit(point) {
  editingPoint.value = point;
  modalError.value = '';
  showEditModal.value = true;
}

async function applyLockedEdit(newFreq) {
  modalBusy.value = true;
  modalError.value = '';
  const ok = await store.setCrossoverFreq(editingPoint.value.id, newFreq, true);
  modalBusy.value = false;
  if (ok) {
    showEditModal.value = false;
  } else {
    // Keep the modal open so the server's rejection is read in context
    modalError.value = store.error || 'The device rejected this change.';
    store.clearError();
  }
}
</script>
