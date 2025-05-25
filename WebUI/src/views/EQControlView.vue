<template>
  <div class="eq-control-view p-2 md:p-4">
    <!-- <h3 class="text-lg font-semibold text-vybes-blue mb-4 text-center sm:text-left">Equalizer Controls</h3> -->
    <ParametricEQ :api-client="apiClient" />
    <!-- Other related controls or information for this view can be added later -->
  </div>
</template>

<script setup>
import { inject } from 'vue';
import ParametricEQ from '../components/ParametricEQ.vue';

const apiClient = inject('vybesAPI');

// Props or API interactions for this view will be handled later
// For example, loading current preset data and passing it to ParametricEQ
// For now, we are just passing the apiClient down
</script>

<style scoped>
/* Scoped styles for the EQControlView if needed */
.eq-control-view {
  width: 100%;
  max-width: 900px; /* Max width for the EQ component to not get too stretched on large screens */
  margin-left: auto;
  margin-right: auto;
}
</style>
