<template>
    <CardSection>
      <ParametricEQ
          v-if="eqPointsForEditor"
          :peq-points="eqPointsForEditor"
          @change="$emit('update-eq-points', $event)"
          class="min-h-[400px] h-auto"
      />
    </CardSection>
  </template>
  
  <script setup>
  import { computed, ref, watch } from 'vue';
  import CardSection from './CardSection.vue';
  import ParametricEQ from '../ParametricEQ.vue';
  
  const props = defineProps({
    eqSets: Array, // Expected format: [{ spl: Number, peqSet: Array }, ...]
  });
  
  const emit = defineEmits(['update-eq-points']);

  const currentSPL = ref(0); // Default SPL when no sets exist
  
  // Compute the current set's EQ points
  const eqPointsForEditor = computed(() => {
    console.log('EQSection - eqPointsForEditor computed - props.eqSets:', props.eqSets);
    if (!props.eqSets || props.eqSets.length === 0) {
      console.log('EQSection - No EQ sets available');
      return [];
    }
    const currentSet = props.eqSets.find(set => set.spl === currentSPL.value) || props.eqSets[0];
    console.log('EQSection - Current set:', currentSet);
    const points = currentSet ? currentSet.peqs || [] : [];
    console.log('EQSection - Returning points:', points);
    return points;
  });

  watch(() => props.eqSets, (newValue) => {
    console.log('EQSection - eqSets prop changed', newValue);
  }, { immediate: true });
  </script>