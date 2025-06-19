<template>
    <CardSection>
      <ParametricEQ
          :eq-points="eqPointsForEditor"
          @update:eq-points="$emit('update-eq-points', $event)"
          class="min-h-[400px] h-auto"
      />
    </CardSection>
  </template>
  
  <script setup>
  import { computed } from 'vue';
  import CardSection from './CardSection.vue';
  import ParametricEQ from '../ParametricEQ.vue';
  
  const props = defineProps({
    eqSets: Array, // Expected format: [{ spl: Number, peqSet: Array }, ...]
  });
  
  const emit = defineEmits(['update-eq-points']);

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