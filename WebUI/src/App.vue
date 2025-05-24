<script setup>
import ParametricEQ from './components/ParametricEQ.vue'
</script>

<template>
  <div id="app-wrapper" class="bg-white dark:bg-gray-800 text-gray-900 dark:text-gray-100 min-h-screen flex flex-col">
    <header class="bg-gray-900 text-white p-4 text-center">
      <h1 class="text-2xl">Vybes</h1>
    </header>
    <main class="p-4 flex-grow">
      <ParametricEQ />
    </main>
    <footer class="bg-gray-700 text-white p-3 text-center text-sm">
      <p>&copy; 2024 Vybes</p>
    </footer>
  </div>
</template>

<style>
/* Minimal global styles, Tailwind will handle most things */
body {
  margin: 0; /* Ensure body takes full width for footer */
}

/* Add any other global styles or overrides here */
#app {
  /* Ensure #app takes full height if it's still the root Vue element,
     but styling is now primarily on app-wrapper.
     If App.vue's root element IS #app, then #app-wrapper is redundant.
     Assuming #app is the mount point from main.js and App.vue's template is injected into it.
  */
}
</style>
