<template>
  <div class="range-slider-group">
    <div class="flex justify-between items-center mb-1">
      <label v-if="label" :for="sliderId" class="text-sm text-vybes-text-secondary">{{ label }}</label>
      <span class="text-xs text-vybes-blue font-medium">{{ displayValue }}</span>
    </div>
    <div class="flex items-center space-x-2">
      <input
        :id="sliderId"
        type="range"
        :min="min"
        :max="max"
        :step="step"
        :value="modelValue"
        @input="$emit('update:modelValue', parseFloat($event.target.value))"
        class="flex-1 h-2 bg-vybes-dark-element rounded-lg appearance-none cursor-pointer
               focus:outline-none focus:ring-2 focus:ring-vybes-blue/50
               [&::-webkit-slider-thumb]:appearance-none
               [&::-webkit-slider-thumb]:w-4
               [&::-webkit-slider-thumb]:h-4
               [&::-webkit-slider-thumb]:bg-vybes-blue
               [&::-webkit-slider-thumb]:rounded-full
               [&::-webkit-slider-thumb]:shadow
               [&::-moz-range-thumb]:w-4
               [&::-moz-range-thumb]:h-4
               [&::-moz-range-thumb]:bg-vybes-blue
               [&::-moz-range-thumb]:rounded-full
               [&::-moz-range-thumb]:border-none
               [&::-moz-range-thumb]:shadow"
      />
      <input
        type="number"
        :min="min"
        :max="max"
        :step="step"
        :value="modelValue"
        @input="$emit('update:modelValue', parseFloat($event.target.value))"
        class="w-20 p-1 text-sm bg-vybes-dark-element border border-vybes-dark-card rounded-md text-vybes-text-primary text-center focus:outline-none focus:ring-1 focus:ring-vybes-blue focus:border-vybes-blue flex-shrink-0"
      />
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue';

const props = defineProps({
  modelValue: { // Used for v-model
    type: Number,
    required: true,
  },
  label: {
    type: String,
    default: '',
  },
  min: {
    type: Number,
    default: 0,
  },
  max: {
    type: Number,
    default: 100,
  },
  step: {
    type: Number,
    default: 1,
  },
  unit: { // e.g., 'Hz', 'dB'
    type: String,
    default: ''
  },
  decimals: { // Number of decimals for displayValue
     type: Number,
     default: 1
  }
});

defineEmits(['update:modelValue']);

const sliderId = computed(() => `range-slider-${Math.random().toString(36).substring(2, 9)}`);

const displayValue = computed(() => {
  let value = props.modelValue;
  if (typeof value === 'number') {
     // Handle potential floating point inaccuracies fortoFixed
     value = parseFloat(value.toFixed(props.decimals + 2)); // Calculate with more precision
     value = parseFloat(value.toFixed(props.decimals)); // Then fix to desired decimals
  }
  return `${value}${props.unit}`;
});
</script>

<style scoped>
/* Scoped styles if needed, Tailwind classes are preferred. */
/* Custom styling for range input track and thumb for Firefox if not covered by Tailwind plugins */
input[type="range"]::-moz-range-track {
  background-color: #222222; /* vybes-dark-element */
  height: 0.5rem;
  border-radius: 0.5rem;
}
/* Note: Tailwind JIT might not pick up all pseudo-elements for range inputs across browsers.
   The provided Tailwind classes cover WebKit (Chrome, Safari, Edge) and attempt to cover Firefox.
   Further testing and specific Firefox pseudo-elements might be needed if it doesn't render as expected.
*/
</style>