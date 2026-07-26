<template>
    <!-- 0-20000 µs (20 ms max); the device truncates to whole µs -->
    <InputGroup
      :model-value="modelValue"
      @update:model-value="$emit('update:modelValue', $event)"
      :label="`${title} (µs)`"
      type="number"
      :min="0"
      :max="20000"
      :step="10"
      :disabled="disabled"
    />
  </template>

  <script setup>
  import InputGroup from './InputGroup.vue';

  defineProps({
    modelValue: Number,
    title: String,
    disabled: {
      type: Boolean,
      default: false,
    },
  });
  defineEmits(['update:modelValue']);
  </script>
