<template>
  <div id="app" class="min-h-screen bg-vybes-dark text-vybes-text-primary flex flex-col">
    <!-- Top bar: brand always, links on desktop only -->
    <nav class="bg-vybes-dark-element border-b border-vybes-dark-input">
      <div class="container mx-auto px-4 py-3 sm:py-4 flex items-center justify-between">
        <router-link
          to="/"
          class="text-xl sm:text-2xl font-bold text-vybes-accent hover:text-vybes-accent-light transition-colors"
        >
          Vybes DSP
        </router-link>

        <div class="hidden sm:flex space-x-6">
          <router-link
            v-for="tab in tabs"
            :key="tab.name"
            :to="tab.to"
            class="nav-link"
            :class="{ 'nav-link-active': isActive(tab) }"
          >
            {{ tab.label }}
          </router-link>
        </div>
      </div>
    </nav>

    <!-- Device connectivity banner -->
    <div
      v-if="showOfflineBanner"
      class="bg-amber-500/15 border-b border-amber-500/30 text-amber-200 text-sm text-center px-4 py-2"
    >
      Device offline — reconnecting…
    </div>

    <!-- Main Content -->
    <main class="flex-1 pb-20 sm:pb-0">
      <router-view />
    </main>

    <!-- Bottom tab bar: thumb-reach navigation on phones -->
    <nav
      class="sm:hidden fixed bottom-0 inset-x-0 z-40 bg-vybes-dark-element border-t border-vybes-dark-input pb-[env(safe-area-inset-bottom)]"
    >
      <div class="grid grid-cols-3">
        <router-link
          v-for="tab in tabs"
          :key="tab.name"
          :to="tab.to"
          class="flex flex-col items-center gap-0.5 py-2 text-[11px] font-medium transition-colors"
          :class="isActive(tab) ? 'text-vybes-primary' : 'text-vybes-text-secondary'"
        >
          <svg class="w-6 h-6" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.8" :d="tab.icon" />
          </svg>
          {{ tab.label }}
        </router-link>
      </div>
    </nav>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue';
import { useRoute } from 'vue-router';
import apiClient from './api-client.js';

const route = useRoute();

const tabs = [
  {
    name: 'Home',
    label: 'Home',
    to: '/',
    // The preset editor is reached from Home, so it keeps the Home tab lit.
    matches: ['Home', 'Preset'],
    icon: 'M3 12l2-2m0 0l7-7 7 7M5 10v10a1 1 0 001 1h3m10-11l2 2m-2-2v10a1 1 0 01-1 1h-3m-6 0a1 1 0 001-1v-4a1 1 0 011-1h2a1 1 0 011 1v4a1 1 0 001 1m-6 0h6'
  },
  {
    name: 'Calibrate',
    label: 'Calibrate',
    to: '/calibrate',
    matches: ['Calibrate'],
    icon: 'M12 6v12m-5-9v6m10-6v6M4.5 10.5v3m15-3v3'
  },
  {
    name: 'Tools',
    label: 'Tools',
    to: '/tools',
    matches: ['Tools'],
    icon: 'M11.42 15.17L17.25 21A2.652 2.652 0 0021 17.25l-5.877-5.877M11.42 15.17l2.496-3.03c.317-.384.74-.626 1.208-.766M11.42 15.17l-4.655 5.653a2.548 2.548 0 11-3.586-3.586l6.837-5.63m5.108-.233c.55-.164 1.163-.188 1.743-.14a4.5 4.5 0 004.486-6.336l-3.276 3.277a3.004 3.004 0 01-2.25-2.25l3.276-3.276a4.5 4.5 0 00-6.336 4.486c.091 1.076-.071 2.264-.904 2.95l-.102.085'
  }
];

const isActive = (tab) => tab.matches.includes(route.name);

// Connection banner. A short grace period avoids flashing "offline" during
// the initial connect on page load.
const connectionState = ref('disconnected');
const graceOver = ref(false);
let unsubscribeStatus = null;

const showOfflineBanner = computed(
  () => graceOver.value && connectionState.value !== 'connected'
);

onMounted(() => {
  unsubscribeStatus = apiClient.onConnectionChange((state) => {
    connectionState.value = state;
  });
  apiClient.ensureLiveConnection();
  setTimeout(() => { graceOver.value = true; }, 2500);
});

onUnmounted(() => {
  if (unsubscribeStatus) unsubscribeStatus();
});
</script>

<style scoped>
.nav-link {
  color: var(--vybes-text-secondary);
  font-weight: 500;
  transition: color 0.3s;
}

.nav-link:hover {
  color: var(--vybes-text-primary);
}

.nav-link-active {
  color: var(--vybes-primary);
}
</style>
