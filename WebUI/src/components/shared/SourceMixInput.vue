<template>
  <div>
    <div class="flex gap-1" role="group" aria-label="Input source">
      <button
        v-for="option in PRESETS"
        :key="option.id"
        type="button"
        :class="['source-button', activePreset === option.id ? 'source-active' : 'source-inactive']"
        @click="applyPreset(option.id)"
      >{{ option.label }}</button>
    </div>
    <div v-if="activePreset === 'custom'" class="mt-2 space-y-2">
      <RangeSlider
        :model-value="modelValue.left * 100"
        label="From Left"
        :min="0" :max="100" :step="5" unit="%" :decimals="0"
        @update:modelValue="update({ left: $event / 100 })"
      />
      <RangeSlider
        :model-value="modelValue.right * 100"
        label="From Right"
        :min="0" :max="100" :step="5" unit="%" :decimals="0"
        @update:modelValue="update({ right: $event / 100 })"
      />
    </div>
  </div>
</template>

<script setup>
import { ref, computed } from 'vue';
import RangeSlider from './RangeSlider.vue';

const props = defineProps({
  /** {left, right} gains 0..1 on the input buses */
  modelValue: { type: Object, required: true },
});
const emit = defineEmits(['update:modelValue']);

const PRESETS = [
  { id: 'left', label: 'Left', mix: { left: 1, right: 0 } },
  { id: 'right', label: 'Right', mix: { left: 0, right: 1 } },
  { id: 'mono', label: 'Mono', mix: { left: 0.5, right: 0.5 } },
  { id: 'custom', label: 'Custom' },
];

// True once the user opens Custom, so a custom mix that happens to match a
// preset doesn't collapse the sliders away mid-edit
const customPinned = ref(false);

const activePreset = computed(() => {
  if (customPinned.value) return 'custom';
  const match = PRESETS.find(
    (p) => p.mix && p.mix.left === props.modelValue.left && p.mix.right === props.modelValue.right
  );
  return match ? match.id : 'custom';
});

function applyPreset(id) {
  if (id === 'custom') {
    customPinned.value = true;
    return;
  }
  customPinned.value = false;
  const preset = PRESETS.find((p) => p.id === id);
  emit('update:modelValue', { ...preset.mix });
}

function update(partial) {
  emit('update:modelValue', { ...props.modelValue, ...partial });
}
</script>

<style scoped>
@reference "../../style.css";

.source-button {
  @apply px-2.5 py-1 rounded-md text-xs font-medium transition-colors;
}
.source-active {
  @apply bg-vybes-primary text-white;
}
.source-inactive {
  @apply bg-vybes-dark-input text-vybes-text-secondary hover:text-vybes-text-primary;
}
</style>
