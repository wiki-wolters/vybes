<template>
  <div>
    <div class="flex justify-between text-xs text-vybes-text-secondary mb-1">
      <span>FIR tap pool</span>
      <span>{{ used.toLocaleString() }} / {{ total.toLocaleString() }}</span>
    </div>
    <div class="h-2 rounded-full bg-vybes-dark-input overflow-hidden">
      <div
        class="h-full rounded-full transition-all duration-300"
        :class="barColor"
        :style="{ width: `${percent}%` }"
      ></div>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue';

const props = defineProps({
  used: { type: Number, required: true },
  total: { type: Number, required: true },
});

const percent = computed(() =>
  props.total > 0 ? Math.min(100, (props.used / props.total) * 100) : 0
);

const barColor = computed(() => {
  if (percent.value >= 100) return 'bg-red-500';
  if (percent.value >= 75) return 'bg-amber-500';
  return 'bg-vybes-primary';
});
</script>
