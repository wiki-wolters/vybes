<template>
  <div v-if="modelValue" class="modal-backdrop" @click.self="cancel"></div>
  <div v-if="modelValue" class="modal-content">
    <h3 class="text-xl font-semibold mb-1 flex items-center gap-2">
      <svg class="w-5 h-5 text-vybes-accent flex-none" fill="none" stroke="currentColor" viewBox="0 0 24 24">
        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 15v2m-6 4h12a2 2 0 002-2v-6a2 2 0 00-2-2H6a2 2 0 00-2 2v6a2 2 0 002 2zm10-10V7a4 4 0 00-8 0v4h8z"></path>
      </svg>
      {{ title }}
    </h3>
    <p class="text-sm text-vybes-text-secondary mb-4">
      This value is locked to protect your drivers. Changes only apply after an explicit confirmation.
    </p>

    <!-- Step 1: enter the new value -->
    <template v-if="step === 'edit'">
      <label class="block text-sm text-vybes-text-secondary mb-1" for="locked-value-input">
        New value ({{ min }}&ndash;{{ max }} {{ unit }})
      </label>
      <input
        id="locked-value-input"
        ref="inputEl"
        v-model.number="draft"
        type="number"
        :min="min"
        :max="max"
        class="w-full rounded-md bg-vybes-dark-input text-vybes-text-primary px-3 py-2 mb-1"
        @keyup.enter="review"
      />
      <p v-if="draftInvalid" class="text-sm text-red-400 mb-2">
        Value must be between {{ min }} and {{ max }} {{ unit }}.
      </p>
      <div class="flex justify-end space-x-3 mt-4">
        <button class="btn-secondary" @click="cancel">Cancel</button>
        <button class="btn-primary" :disabled="draftInvalid || draft === value" @click="review">Save</button>
      </div>
    </template>

    <!-- Step 2: review and confirm -->
    <template v-else>
      <div class="rounded-md bg-vybes-dark-element p-3 mb-3 text-center">
        <span class="text-vybes-text-secondary">{{ value }} {{ unit }}</span>
        <span class="mx-2 text-vybes-text-secondary">&rarr;</span>
        <span class="text-vybes-text-primary font-semibold">{{ draft }} {{ unit }}</span>
      </div>
      <div v-if="affected.length" class="mb-3">
        <p class="text-sm text-vybes-text-secondary mb-1">This will retune:</p>
        <ul class="text-sm text-vybes-text-primary space-y-0.5">
          <li v-for="item in affected" :key="item" class="flex items-center gap-2">
            <span class="w-1.5 h-1.5 rounded-full bg-vybes-accent flex-none"></span>{{ item }}
          </li>
        </ul>
      </div>
      <p v-if="error" class="text-sm text-red-400 mb-2">{{ error }}</p>
      <div class="flex justify-end space-x-3 mt-4">
        <button class="btn-secondary" @click="step = 'edit'">Back</button>
        <button class="btn-danger" :disabled="busy" @click="$emit('confirm', draft)">
          {{ busy ? 'Applying…' : 'Confirm & Apply' }}
        </button>
      </div>
    </template>
  </div>
</template>

<script setup>
import { ref, computed, watch, nextTick } from 'vue';

const props = defineProps({
  modelValue: { type: Boolean, required: true },
  title: { type: String, required: true },
  value: { type: Number, required: true },
  unit: { type: String, default: 'Hz' },
  min: { type: Number, required: true },
  max: { type: Number, required: true },
  /** Human-readable list of filters this change retunes */
  affected: { type: Array, default: () => [] },
  /** Server rejection to display on the confirm step */
  error: { type: String, default: '' },
  /** True while the confirm request is in flight */
  busy: { type: Boolean, default: false },
});

const emit = defineEmits(['update:modelValue', 'confirm']);

const step = ref('edit');
const draft = ref(props.value);
const inputEl = ref(null);

const draftInvalid = computed(() =>
  typeof draft.value !== 'number' || Number.isNaN(draft.value)
  || draft.value < props.min || draft.value > props.max
);

watch(() => props.modelValue, async (open) => {
  if (open) {
    step.value = 'edit';
    draft.value = props.value;
    await nextTick();
    inputEl.value?.focus();
  }
});

function review() {
  if (!draftInvalid.value && draft.value !== props.value) step.value = 'confirm';
}

function cancel() {
  emit('update:modelValue', false);
}
</script>

<style scoped>
@reference "../../style.css";

.modal-backdrop {
  position: fixed;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  background-color: rgba(0, 0, 0, 0.5);
  z-index: 50;
}

.modal-content {
  background-color: var(--vybes-dark-card, #1a1a1a);
  border-radius: 0.5rem;
  padding: 1.5rem;
  max-width: 500px;
  width: 90%;
  box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.3);
  z-index: 51;
  position: fixed;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
}

.btn-danger {
  @apply px-4 py-2 rounded-md font-medium bg-red-600 hover:bg-red-500 text-white disabled:opacity-50 disabled:cursor-not-allowed;
}
</style>
