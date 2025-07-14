<template>
  <div class="input-source-slider-group">
    <div class="flex justify-between items-center mb-1">
      <span class="text-sm text-vybes-text-secondary">{{ leftLabel }}</span>
      <span class="text-sm text-vybes-text-secondary">{{ rightLabel }}</span>
    </div>
    <div class="flex items-center space-x-2">
      <input
        type="range"
        min="0"
        max="1"
        step="0.01"
        :value="modelValue"
        @input="$emit('update:modelValue', parseFloat($event.target.value))"
        class="flex-1 h-2 bg-vybes-dark-input rounded-lg appearance-none cursor-pointer
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
    </div>
  </div>
</template>

<script setup>
defineProps({
  modelValue: {
    type: Number,
    required: true,
  },
  leftLabel: {
    type: String,
    default: 'Left',
  },
  rightLabel: {
    type: String,
    default: 'Right',
  },
});

defineEmits(['update:modelValue']);
</script>
