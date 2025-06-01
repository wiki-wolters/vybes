<template>
  <div v-if="modelValue" class="modal-backdrop" @click.self="$emit('update:modelValue', false)">
    <div class="modal-content">
      <h3 class="text-xl font-semibold mb-4">{{ title }}</h3>
      <slot></slot>
      <div class="flex justify-end space-x-3 mt-4">
        <button @click="$emit('update:modelValue', false)" class="btn-secondary">{{ cancelText }}</button>
        <button @click="$emit('confirm')" class="btn-primary">{{ confirmText }}</button>
      </div>
    </div>
  </div>
</template>

<script setup>
defineProps({
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

defineEmits(['update:modelValue', 'confirm']);
</script>

<style scoped>
/* Inherit existing modal styles from the application */
.modal-backdrop {
  position: fixed;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  background-color: rgba(0, 0, 0, 0.5);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 50;
}

.modal-content {
  background-color: var(--vybes-dark-card, #1a1a1a);
  border-radius: 0.5rem;
  padding: 1.5rem;
  max-width: 500px;
  width: 90%;
  box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.3);
}
</style>
