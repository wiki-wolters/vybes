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
        :min="sliderMin"
        :max="sliderMax"
        :step="sliderStep"
        :value="sliderValue"
        @input="sliderValue = parseFloat($event.target.value)"
        class="flex-1 h-2 bg-vybes-dark-input rounded-lg appearance-none cursor-pointer select-none
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
        :value="roundedModelValue"
        @input="emitRoundedValue(parseFloat($event.target.value))"
        class="w-20 p-1 text-sm bg-vybes-dark-input border border-vybes-dark-card rounded-md text-vybes-text-primary text-center focus:outline-none focus:ring-1 focus:ring-vybes-blue focus:border-vybes-blue flex-none"
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
     default: 2
  },
  logarithmic: {
    type: Boolean,
    default: false
  }
});

const emit = defineEmits(['update:modelValue']);

function emitRoundedValue(value) {
  if (typeof value === 'number' && !isNaN(value)) {
    const rounded = parseFloat(value.toFixed(props.decimals));
    // Clamp value between min and max
    const clamped = Math.max(props.min, Math.min(props.max, rounded));
    if (clamped !== props.modelValue) {
        emit('update:modelValue', clamped);
    }
  }
}

const sliderId = computed(() => `range-slider-${Math.random().toString(36).substring(2, 9)}`);

const sliderValue = computed({
  get() {
    if (props.logarithmic) {
      // Handle case where modelValue could be 0 or negative for log scale
      return Math.log10(Math.max(props.min, props.modelValue));
    }
    return props.modelValue;
  },
  set(val) {
    let newValue;
    if (props.logarithmic) {
      newValue = Math.pow(10, val);
    } else {
      newValue = val;
    }
    emitRoundedValue(newValue);
  }
});

const sliderMin = computed(() => props.logarithmic ? Math.log10(props.min) : props.min);
const sliderMax = computed(() => props.logarithmic ? Math.log10(props.max) : props.max);
const sliderStep = computed(() => {
    if (props.logarithmic) {
        // Provide a reasonable number of steps for smoothness
        return (sliderMax.value - sliderMin.value) / 1000;
    }
    return props.step;
});

const roundedModelValue = computed(() => {
  if (typeof props.modelValue !== 'number') {
    return props.modelValue;
  }
  return parseFloat(props.modelValue.toFixed(props.decimals));
});

const displayValue = computed(() => {
  return `${roundedModelValue.value}${props.unit}`;
});
</script>

<style scoped>
input[type="range"] {
  -webkit-touch-callout: none;
}
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
