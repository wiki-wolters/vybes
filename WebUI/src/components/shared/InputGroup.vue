<template>
  <div class="input-group">
    <label v-if="label" :for="inputId" class="block text-sm text-vybes-text-secondary mb-1">{{ label }}</label>
    <input 
      :id="inputId"
      :type="type"
      :value="modelValue"
      @input="handleInput"
      :min="min"
      :max="max"
      :step="step"
      :placeholder="placeholder"
      :disabled="disabled"
      class="w-full px-3 py-2 bg-vybes-dark-input border border-vybes-dark-border rounded-md text-vybes-text-primary"
    >
  </div>
</template>

<script setup>
import { computed } from 'vue';

const props = defineProps({
  modelValue: {
    type: [String, Number],
    required: true,
  },
  label: {
    type: String,
    default: '',
  },
  type: {
    type: String,
    default: 'text',
  },
  min: {
    type: [Number, String],
    default: undefined,
  },
  max: {
    type: [Number, String],
    default: undefined,
  },
  step: {
    type: [Number, String],
    default: undefined,
  },
  placeholder: {
    type: String,
    default: '',
  },
  disabled: {
    type: Boolean,
    default: false,
  }
});

const emit = defineEmits(['update:modelValue']);

const inputId = computed(() => `input-${Math.random().toString(36).substring(2, 9)}`);

const handleInput = (event) => {
  const value = props.type === 'number' 
    ? parseFloat(event.target.value) 
    : event.target.value;
  emit('update:modelValue', value);
};
</script>
