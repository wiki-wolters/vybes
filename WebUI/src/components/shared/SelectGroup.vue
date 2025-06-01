<template>
  <div class="select-group">
    <label v-if="label" :for="selectId" class="block text-sm text-vybes-text-secondary mb-1">{{ label }}</label>
    <select
      :id="selectId"
      :value="modelValue"
      @change="$emit('update:modelValue', $event.target.value)"
      :disabled="disabled"
      class="w-full px-3 py-2 bg-vybes-dark-input border border-vybes-dark-border rounded-md text-vybes-text-primary"
    >
      <slot></slot>
    </select>
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
  disabled: {
    type: Boolean,
    default: false,
  }
});

defineEmits(['update:modelValue']);

const selectId = computed(() => `select-${Math.random().toString(36).substring(2, 9)}`);
</script>
