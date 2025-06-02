<template>
    <CardSection :title="title">
      <div class="mb-4">
        <div class="flex justify-between items-center mb-2">
          <span class="text-sm text-vybes-text-secondary">EQ Sets:</span>
        </div>
        <div v-if="sortedEqSets && sortedEqSets.length > 0" class="flex flex-wrap gap-2">
          <button
            v-for="set in sortedEqSets"
            :key="set.spl"
            @click="$emit('select-set', set.spl)"
            :class="['px-3 py-1 rounded-md text-sm',
              currentSpl === set.spl ? 'bg-vybes-primary text-white' : 'bg-vybes-dark-input text-vybes-text-secondary hover:bg-vybes-dark-hover']"
          >
            {{ set.spl }}
          </button>
          <button @click="promptAndAddNewSet" class="btn-secondary btn-sm py-1 px-2">Add</button>
        </div>
        <div v-else class="text-sm text-vybes-text-secondary italic">No EQ sets defined. Add one to get started.</div>
      </div>
  
      <ParametricEQ
          :eq-points="eqPointsForEditor"
          @update:eq-points="$emit('update-eq-points', $event)"
          class="min-h-[400px] h-auto"
      />
  
      <template #actions>
        <button 
          @click="$emit('delete-set')" 
          class="btn-danger" 
          v-if="eqSets && eqSets.some(set => set.spl === currentSpl)"
        >
          Delete {{ currentSpl }} SPL Set
        </button>
      </template>
    </CardSection>
  </template>
  
  <script setup>
  import { computed } from 'vue';
  import CardSection from './CardSection.vue';
  import ParametricEQ from '../ParametricEQ.vue';
  
  const props = defineProps({
    title: String,
    eqSets: Array, // Expected format: [{ spl: Number, peqSet: Array }, ...]
  });
  
  const emit = defineEmits(['update-eq-points', 'delete-set', 'create-new-set']);

  const sortedEqSets = computed(() => {
    if (!props.eqSets) return [];
    return [...props.eqSets].sort((a, b) => a.spl - b.spl);
  });
  
  function handleSPLInputChange(value) {
    // This handles changing the SPL of the *current* set (effectively renaming it if the new SPL doesn't exist)
    // or selecting an existing set if the new SPL matches one.
    // The parent (PresetEditorView) contains the logic for this.
    const newSpl = parseInt(value, 10);
    if (!isNaN(newSpl) && newSpl >= 0 && newSpl <= 120) {
        emit('update-current-spl', newSpl);
    }
  }

  function promptAndAddNewSet() {
    const newSplString = window.prompt('Enter SPL for the new EQ set (e.g., 65, 75, 85). This will create a new, empty set.');
    if (newSplString) {
      const newSpl = parseInt(newSplString, 10);
      if (!isNaN(newSpl) && newSpl >= 0 && newSpl <= 120) {
        if (props.eqSets && props.eqSets.some(set => set.spl === newSpl)) {
          alert(`An EQ set for SPL ${newSpl} already exists. Please select it from the list or choose a different SPL for a new set.`);
          // Optionally, emit select-set to auto-select it if preferred:
          // emit('select-set', newSpl);
        } else {
          emit('create-new-set', newSpl);
        }
      } else {
        alert('Invalid SPL value. Please enter a number between 0 and 120.');
      }
    }
  }
  </script>