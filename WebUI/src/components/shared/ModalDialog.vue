<template>
  <div v-if="modelValue" class="modal-backdrop" @click.self="close"></div>
  <div
    v-if="modelValue"
    ref="dialogRef"
    class="modal-content"
    role="dialog"
    aria-modal="true"
    :aria-label="title"
    tabindex="-1"
    @keydown.tab="wrapTab"
  >
    <h3 class="text-xl font-semibold mb-4">{{ title }}</h3>
    <slot></slot>
    <div class="flex justify-end space-x-3 mt-4">
      <button @click="close" class="btn-secondary">{{ cancelText }}</button>
      <button @click="$emit('confirm')" class="btn-primary">{{ confirmText }}</button>
    </div>
  </div>
</template>

<script setup>
import { ref, watch, onUnmounted, nextTick } from 'vue';

const props = defineProps({
  modelValue: {
    type: Boolean,
    required: true
  },
  title: {
    type: String,
    required: true
  },
  cancelText: {
    type: String,
    default: 'Cancel'
  },
  confirmText: {
    type: String,
    default: 'Confirm'
  }
});

const emit = defineEmits(['update:modelValue', 'confirm']);

const dialogRef = ref(null);
let previouslyFocused = null;

const close = () => emit('update:modelValue', false);

// Listening on the window rather than the dialog means Escape works even if
// something inside the dialog has moved focus elsewhere.
function onKeydown(event) {
  if (event.key === 'Escape') {
    event.preventDefault();
    close();
  }
}

const FOCUSABLE =
  'a[href], button:not([disabled]), input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])';

/** Minimal focus containment — no dependency, no full focus-trap semantics. */
function wrapTab(event) {
  const candidates = Array.from(dialogRef.value?.querySelectorAll(FOCUSABLE) ?? [])
    .filter((el) => el.offsetWidth > 0 || el.offsetHeight > 0);
  if (candidates.length === 0) return;

  const first = candidates[0];
  const last = candidates[candidates.length - 1];
  const active = document.activeElement;

  if (event.shiftKey && (active === first || active === dialogRef.value)) {
    event.preventDefault();
    last.focus();
  } else if (!event.shiftKey && active === last) {
    event.preventDefault();
    first.focus();
  }
}

watch(() => props.modelValue, async (open) => {
  if (open) {
    previouslyFocused = document.activeElement;
    window.addEventListener('keydown', onKeydown);
    await nextTick();
    // Callers that focus a specific field (e.g. a name input) win; only
    // take focus when nothing inside the dialog has claimed it.
    if (!dialogRef.value?.contains(document.activeElement)) dialogRef.value?.focus();
  } else {
    window.removeEventListener('keydown', onKeydown);
    previouslyFocused?.focus?.();
    previouslyFocused = null;
  }
});

onUnmounted(() => window.removeEventListener('keydown', onKeydown));
</script>

<style scoped>
.modal-backdrop {
  position: fixed;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  background-color: rgba(0, 0, 0, 0.5);
  z-index: 50;
}

/* Fixed, not absolute: the dialog centres in the viewport no matter how far
   the page behind it is scrolled. */
.modal-content {
  background-color: var(--vybes-dark-card);
  border: 1px solid var(--vybes-border);
  border-radius: 0.5rem;
  padding: 1.5rem;
  max-width: 500px;
  width: 90%;
  max-height: 90vh;
  overflow-y: auto;
  box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.5);
  z-index: 51;
  position: fixed;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
}

.modal-content:focus {
  outline: none;
}
</style>
