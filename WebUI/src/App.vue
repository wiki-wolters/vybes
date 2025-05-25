<template>
  <div class="flex flex-col min-h-screen bg-vybes-dark-bg text-vybes-text-primary">
    <AppHeader />

    <!-- Main content area -->
    <main class="flex-grow w-full">
      <EQControlView />
    </main>

    <!-- Placeholder for a possible Footer or Bottom Navigation -->
    <footer class="w-full p-3 text-center text-xs text-vybes-text-secondary bg-vybes-dark-element">
      Vybes App &copy; 2024 (Footer Placeholder)
    </footer>
  </div>
</template>

<script setup>
import { provide, onMounted, onUnmounted } from 'vue'; // Add onMounted, onUnmounted
import AppHeader from './components/layout/AppHeader.vue';
import EQControlView from './views/EQControlView.vue';
import { useVybesAPI } from './composables/useVybesAPI.js';

const { apiClient, connectWebSocket, disconnectWebSocket, liveUpdateData, isWebSocketConnected } = useVybesAPI();

provide('vybesAPI', apiClient); // Already providing apiClient

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
/* Ensure global styles from style.css are applied.
   No additional App.vue specific styles needed for this basic layout if already handled in style.css.
   If you need to override or add, you can do so here.
   For example, ensuring the container takes up available space if not using mx-auto for main content.
*/

/* Minimal global styles are in style.css - body background and text color are set there.
   #app is the mount point in index.html, App.vue's template replaces its content.
   The root div here now controls the overall app appearance.
*/
body {
  /* Overrides for body from style.css can be placed here if absolutely necessary,
     but it's better to manage global body styles in style.css.
     The font-family and min-height are already set in style.css.
     The bg-vybes-dark-bg and text-vybes-text-primary are applied on the root div,
     which is fine as it's more specific than body.
  */
  margin: 0; /* This is a common reset, ensure it's in style.css or here if needed. */
}
</style>
