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

function updateBalance(value) {
  balance.value = value;
  const left = value > 0 ? 100 - value : 100;
  const right = value < 0 ? 100 + value : 100;
  gains.left = left;
  gains.right = right;
  emit('update:gains', { ...gains });
}

function updateGain(speaker, value) {
  gains[speaker] = value;
  emit('update:gains', { ...gains });
}

watch(() => props.initialGains, (newGains) => {
  Object.assign(gains, newGains);
}, { deep: true });
</script>
