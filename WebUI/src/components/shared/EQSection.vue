<template>
    <CardSection :title="title">
      <template #header-actions>
        <div class="flex items-center space-x-2">
          <label class="text-sm text-vybes-text-secondary">SPL:</label>
          <input
            type="number"
            :value="currentSpl"
            @change="handleSPLInputChange($event.target.value)"
            min="0"
            max="120"
            step="1"
            class="w-20 px-2 py-1 bg-vybes-dark-input border border-vybes-dark-border rounded-md text-vybes-text-primary text-sm"
          >
        </div>
      </template>
  
      <div v-if="eqSets && eqSets.length > 0" class="mb-4">
        <div class="text-sm text-vybes-text-secondary mb-2">Available EQ Sets:</div>
        <div class="flex flex-wrap gap-2">
          <button
            v-for="set in eqSets"
            :key="set.spl"
            @click="$emit('select-set', set.spl)"
            :class="['px-3 py-1 rounded-md text-sm',
              currentSpl === set.spl ? 'bg-vybes-primary text-white' : 'bg-vybes-dark-input text-vybes-text-secondary hover:bg-vybes-dark-hover']"
          >
            {{ set.spl }} SPL
          </button>
        </div>
      </div>
  
        <ParametricEQ
            :eq-points="eqPointsForEditor"
            @update:eq-points="$emit('update-eq-points', $event)"
            class="min-h-[400px] h-auto"
        />
  
      <template #actions>
        <button @click="$emit('delete-set')" class="btn-danger" v-if="eqSets && eqSets.some(set => set.spl === currentSpl)">Delete This EQ Set</button>
      </template>
    </CardSection>
  </template>
  
  <script setup>
  import CardSection from './CardSection.vue'; // Assuming path
  import ParametricEQ from '../ParametricEQ.vue'; // Assuming path
  
  const props = defineProps({
    title: String,
    eqSets: Array,
    currentSpl: Number,
    eqPointsForEditor: Array,
  });
  
  const emit = defineEmits(['update-current-spl', 'select-set', 'update-eq-points', 'delete-set']);
  
  function handleSPLInputChange(value) {
    emit('update-current-spl', parseInt(value, 10));
  }
  </script>