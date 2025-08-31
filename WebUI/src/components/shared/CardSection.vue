<template>
  <section class="mb-3 p-3 bg-vybes-dark-card rounded-lg">
    <div v-if="title" class="flex justify-between items-center mb-4">
      <h3 class="text-lg font-semibold text-vybes-accent">{{ title }}</h3>
      <slot name="header-actions"></slot>
    </div>
    
    <slot></slot>
    
    <div v-if="$slots.actions" class="mt-4 flex justify-end space-x-2">
      <slot name="actions"></slot>
    </div>
  </section>
</template>

<script setup>
defineProps({
  title: {
    type: String,
    default: ''
  }
});
</script>
