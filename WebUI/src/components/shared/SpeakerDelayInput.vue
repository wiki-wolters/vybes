<template>
    <div class="p-3 bg-vybes-dark-element rounded-lg shadow-sm">
      <h4 class="text-md font-medium mb-3 text-vybes-light-blue">{{ title }}</h4>
      <!-- 0-20000 µs (20 ms max); the device truncates to whole µs -->
      <InputGroup
        :model-value="modelValue"
        @update:model-value="$emit('update:modelValue', $event)"
        label="Delay (&mu;s)"
        type="number"
        :min="0"
        :max="20000"
        :step="10"
      />
    </div>
  </template>
  
  <script setup>
  import InputGroup from './InputGroup.vue'; // Assuming path
  
  defineProps({
    modelValue: Number,
    title: String,
  });
  defineEmits(['update:modelValue']);
  </script>