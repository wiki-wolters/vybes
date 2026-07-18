<template>
  <div class="space-y-4">
    <RangeSlider
      :model-value="balance"
      label="L/R Balance"
      :min="-100"
      :max="100"
      :step="1"
      unit="%"
      @update:modelValue="updateBalance"
    />
    <RangeSlider
      :model-value="gains.left"
      label="Left Volume"
      :min="0"
      :max="100"
      :step="1"
      unit="%"
      @update:modelValue="updateGain('left', $event)"
    />
    <RangeSlider
      :model-value="gains.right"
      label="Right Volume"
      :min="0"
      :max="100"
      :step="1"
      unit="%"
      @update:modelValue="updateGain('right', $event)"
    />
    <RangeSlider
      :model-value="gains.sub"
      label="Sub Volume"
      :min="0"
      :max="100"
      :step="1"
      unit="%"
      @update:modelValue="updateGain('sub', $event)"
    />
  </div>
</template>

<script setup>
import { ref, reactive, watch, defineProps, defineEmits } from 'vue';
import RangeSlider from './RangeSlider.vue';

const props = defineProps({
  initialGains: {
    type: Object,
    default: () => ({ left: 100, right: 100, sub: 100 }),
  },
});

const emit = defineEmits(['update:gains']);

const gains = reactive({ ...props.initialGains });
const balance = ref(0);

function calculateBalance() {
  const left = gains.left;
  const right = gains.right;
  if (left > right) {
    return left > 0 ? -(1 - right / left) * 100 : 0;
  } else if (right > left) {
    return right > 0 ? (1 - left / right) * 100 : 0;
  } else {
    return 0;
  }
}

balance.value = calculateBalance();

function updateBalance(value) {
  balance.value = value;
  // Scale from the current level rather than a fixed 100% baseline, so
  // nudging balance attenuates one side of the existing levels instead of
  // jumping both channels to full scale.
  const level = Math.max(gains.left, gains.right);
  gains.left = value > 0 ? Math.round(level * (100 - value) / 100) : level;
  gains.right = value < 0 ? Math.round(level * (100 + value) / 100) : level;
  emit('update:gains', { ...gains });
}

function updateGain(speaker, value) {
  gains[speaker] = value;
  if (speaker === 'left' || speaker === 'right') {
    balance.value = calculateBalance();
  }
  emit('update:gains', { ...gains });
}

watch(() => props.initialGains, (newGains) => {
  Object.assign(gains, newGains);
  balance.value = calculateBalance();
}, { deep: true });
</script>
