<template>
    <div class="p-4 bg-vybes-dark-element rounded-lg shadow-sm">
      <h4 class="text-md font-medium mb-3 text-vybes-light-blue">{{ title }}</h4>
      <InputGroup
        :model-value="modelValue"
        @update:model-value="$emit('update:modelValue', $event)"
        label="Delay (&mu;s)"
        type="number"
        :min="0"
        :max="100"
        :step="0.1"
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