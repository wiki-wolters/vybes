<template>
  <div>
    <label class="block text-sm text-vybes-text-secondary mb-1" for="template-select">Speaker setup</label>
    <select
      id="template-select"
      :value="modelValue"
      class="w-full rounded-md bg-vybes-dark-input text-vybes-text-primary px-3 py-2"
      @change="$emit('update:modelValue', $event.target.value)"
    >
      <option v-for="template in templates" :key="template.id" :value="template.id">
        {{ template.label }}
      </option>
    </select>
    <p v-if="selected" class="text-xs text-vybes-text-secondary mt-1">
      {{ selected.description }} Uses {{ selected.outputsUsed }} of 8 outputs.
    </p>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue';
import apiClient from '../../api-client.js';

const props = defineProps({
  modelValue: { type: String, required: true },
});
defineEmits(['update:modelValue']);

const templates = ref([]);

const selected = computed(() =>
  templates.value.find((t) => t.id === props.modelValue) || null
);

onMounted(async () => {
  try {
    templates.value = await apiClient.getTemplates();
  } catch (e) {
    console.error('Failed to load template list:', e);
    // The select stays empty; the server applies its default template
    templates.value = [];
  }
});
</script>
