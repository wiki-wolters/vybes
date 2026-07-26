<template>
  <div class="range-slider-group">
    <div class="flex items-baseline justify-between gap-3 mb-1.5">
      <label v-if="label" :for="sliderId" class="text-sm text-vybes-text-secondary truncate">{{ label }}</label>
      <input
        v-if="isEditing"
        ref="numberInput"
        v-model="draft"
        type="number"
        inputmode="decimal"
        :aria-label="label"
        :min="min"
        :max="max"
        :step="step"
        class="value-input"
        @keydown.enter.prevent="commitEdit"
        @keydown.esc.prevent="cancelEdit"
        @blur="commitEdit"
      />
      <button v-else type="button" class="value-display" :disabled="disabled" @click="startEdit">
        {{ displayValue }}
      </button>
    </div>
    <input
      :id="sliderId"
      type="range"
      :min="sliderMin"
      :max="sliderMax"
      :step="sliderStep"
      :value="sliderValue"
      :disabled="disabled"
      @input="sliderValue = parseFloat($event.target.value)"
      class="w-full h-2 bg-vybes-dark-input rounded-lg appearance-none cursor-pointer select-none
             disabled:opacity-50 disabled:cursor-not-allowed
             focus:outline-none focus:ring-2 focus:ring-vybes-blue/50
             [&::-webkit-slider-thumb]:appearance-none
             [&::-webkit-slider-thumb]:w-5
             [&::-webkit-slider-thumb]:h-5
             [&::-webkit-slider-thumb]:bg-vybes-blue
             [&::-webkit-slider-thumb]:rounded-full
             [&::-webkit-slider-thumb]:shadow
             [&::-moz-range-thumb]:w-5
             [&::-moz-range-thumb]:h-5
             [&::-moz-range-thumb]:bg-vybes-blue
             [&::-moz-range-thumb]:rounded-full
             [&::-moz-range-thumb]:border-none
             [&::-moz-range-thumb]:shadow"
    />
  </div>
</template>

<script setup>
import { computed, ref, nextTick } from 'vue';
import { formatValue } from '../../utilities.js';

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
  unit: { // e.g., 'Hz', 'dB', '%'
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
  },
  disabled: {
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

const displayValue = computed(() =>
  formatValue(props.modelValue, props.unit, props.decimals)
);

// Tap/click the readout to type an exact value. Enter or blur commits,
// Escape cancels; commitEdit is a no-op once the edit has been abandoned,
// so the blur that follows Escape can't resurrect the draft.
const isEditing = ref(false);
const numberInput = ref(null);
const draft = ref('');

async function startEdit() {
  if (props.disabled) return;
  draft.value = String(roundedModelValue.value);
  isEditing.value = true;
  await nextTick();
  numberInput.value?.focus();
  numberInput.value?.select();
}

function commitEdit() {
  if (!isEditing.value) return;
  isEditing.value = false;
  const parsed = parseFloat(draft.value);
  if (!Number.isNaN(parsed)) emitRoundedValue(parsed);
}

function cancelEdit() {
  isEditing.value = false;
}
</script>

<style scoped>
@reference "../../style.css";

input[type="range"] {
  -webkit-touch-callout: none;
  /* Horizontal drags belong to the slider, never the page (stops iOS
     treating a thumb drag as a pan/rubber-band); pan-y keeps vertical
     page scrolling alive on slider-dense views like Home. */
  touch-action: pan-y;
}

/* Tailwind's arbitrary variants above don't reach Firefox's track */
input[type="range"]::-moz-range-track {
  background-color: var(--vybes-dark-input);
  height: 0.5rem;
  border-radius: 0.5rem;
}

.value-display {
  @apply flex-none -mx-1 px-1 rounded text-sm font-medium text-vybes-blue tabular-nums
         cursor-pointer hover:bg-vybes-dark-input/70
         focus:outline-none focus:ring-2 focus:ring-vybes-blue/50
         disabled:opacity-50 disabled:cursor-not-allowed disabled:hover:bg-transparent;
}

.value-input {
  @apply flex-none w-24 px-1.5 py-0.5 rounded text-sm text-right tabular-nums
         bg-vybes-dark-input border border-vybes-blue text-vybes-text-primary
         focus:outline-none focus:ring-1 focus:ring-vybes-blue;
}

.value-input::-webkit-inner-spin-button,
.value-input::-webkit-outer-spin-button {
  -webkit-appearance: none;
  margin: 0;
}

.value-input {
  -moz-appearance: textfield;
}
</style>
