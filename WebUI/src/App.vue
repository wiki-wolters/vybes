<template>
  <div class="flex flex-col min-h-screen bg-vybes-dark-bg text-vybes-text-primary">
    <AppHeader />

    <nav class="bg-vybes-dark-element p-4">
      <ul class="flex space-x-4 justify-center">
        <li v-for="view in views" :key="view">
          <button
            @click="currentView = view"
            :class="[
              'px-4 py-2 rounded-md text-sm font-medium',
              currentView === view
                ? 'bg-vybes-primary text-white'
                : 'text-vybes-text-secondary hover:bg-vybes-dark-hover hover:text-vybes-text-primary'
            ]"
          >
            {{ view }}
          </button>
        </li>
      </ul>
    </nav>

    <!-- Main content area -->
    <main class="flex-grow w-full p-4">
      <HomeView v-if="currentView === 'Home'" />
      <CalibrationView v-if="currentView === 'Calibration'" />
      <ToolsView v-if="currentView === 'Tools'" />
      <EQControlView v-if="currentView === 'EQ Control'" />
      <PresetEditorView v-if="currentView === 'Preset Editor'" />
      <DataStructuresView v-if="currentView === 'Data Structures'" />
    </main>

    <!-- Placeholder for a possible Footer or Bottom Navigation -->
    <footer class="w-full p-3 text-center text-xs text-vybes-text-secondary bg-vybes-dark-element">
      Vybes App &copy; 2024 (Footer Placeholder)
    </footer>
  </div>
</template>

<script setup>
import { ref, provide, onMounted, onUnmounted } from 'vue'; // Add ref
import AppHeader from './components/layout/AppHeader.vue';
import EQControlView from './views/EQControlView.vue';
import HomeView from './views/HomeView.vue';
import CalibrationView from './views/CalibrationView.vue';
import ToolsView from './views/ToolsView.vue';
import PresetEditorView from './views/PresetEditorView.vue';
import DataStructuresView from './views/DataStructuresView.vue';
import { useVybesAPI } from './composables/useVybesAPI.js';

const currentView = ref('Home');
const views = [
  'Home',
  'Calibration',
  'Tools',
  'EQ Control',
  'Preset Editor',
  'Data Structures'
];

const { apiClient, connectWebSocket, disconnectWebSocket, liveUpdateData, isWebSocketConnected } = useVybesAPI();

provide('vybesAPI', apiClient);
provide('liveUpdateData', liveUpdateData); // Add this
provide('isWebSocketConnected', isWebSocketConnected); // Add this

onMounted(() => {
  connectWebSocket();
});

onUnmounted(() => {
  disconnectWebSocket();
});

// For demonstration, you could watch liveUpdateData here or pass it down
// watch(liveUpdateData, (newData) => {
//   console.log('App.vue: Live data changed', newData);
// });
</script>

<style>
/* Global styles are primarily in style.css.
   App.vue specific styles can be added here if necessary.
*/
body {
  margin: 0; /* Common reset */
}

/* Additional styling for nav can be added here or in a scoped style block if preferred */
</style>
