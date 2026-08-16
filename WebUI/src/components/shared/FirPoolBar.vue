<template>
  <div>
    <div class="flex justify-between text-xs text-vybes-text-secondary mb-1">
      <span>FIR tap pool</span>
      <span class="tabular-nums">{{ formatValue(used, '', 0) }} / {{ formatValue(total, '', 0) }}</span>
    </div>
    <div class="h-2 rounded-full bg-vybes-dark-input overflow-hidden">
      <div
        class="h-full rounded-full transition-all duration-300"
        :class="barColor"
        :style="{ width: `${percent}%` }"
      ></div>
    </div>

    <!-- A channel whose filter failed to load is running uncorrected. That
         used to be visible only on the Teensy's debug console. -->
    <div
      v-if="errors.length"
      class="mt-2 rounded-md border border-red-500/50 bg-red-500/10 px-3 py-2 text-xs text-red-300"
      role="alert"
    >
      <div class="font-semibold mb-1">
        {{ errors.length }} FIR {{ errors.length === 1 ? 'filter' : 'filters' }} failed to load —
        {{ errors.length === 1 ? 'that output is' : 'those outputs are' }} running uncorrected
      </div>
      <ul class="space-y-0.5">
        <li v-for="err in errors" :key="err.output">
          Output <span class="tabular-nums">{{ err.output + 1 }}</span>:
          <span class="font-mono">{{ err.file }}</span> — {{ reasonText(err.code) }}
        </li>
      </ul>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue';
import { formatValue } from '../../utilities.js';

const props = defineProps({
  used: { type: Number, required: true },
  total: { type: Number, required: true },
  errors: { type: Array, default: () => [] },
});

// Codes come from the Teensy's FIRERR lines (see reportFirError)
const REASONS = {
  nosd: 'SD card not readable',
  missing: 'file not found on the SD card',
  toobig: 'too large for the remaining tap pool',
  poolfull: 'tap pool already full',
  nomem: 'not enough memory',
};

function reasonText(code) {
  return REASONS[code] ?? `load failed (${code})`;
}

const percent = computed(() =>
  props.total > 0 ? Math.min(100, (props.used / props.total) * 100) : 0
);

const barColor = computed(() => {
  if (percent.value >= 100) return 'bg-red-500';
  if (percent.value >= 75) return 'bg-amber-500';
  return 'bg-vybes-primary';
});
</script>
