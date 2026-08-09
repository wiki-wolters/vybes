<template>
  <div id="app" class="min-h-screen bg-vybes-dark text-vybes-text-primary flex flex-col">
    <!-- Top bar: brand always, links on desktop only -->
    <nav class="bg-vybes-dark-element border-b border-vybes-dark-input">
      <div class="container mx-auto px-4 py-3 sm:py-4 flex items-center justify-between gap-3">
        <div class="flex items-center gap-2 min-w-0">
          <router-link
            to="/"
            class="text-xl sm:text-2xl font-bold text-vybes-accent hover:text-vybes-accent-light transition-colors"
          >
            Vybes DSP
          </router-link>

          <!-- Dim is a live state, so it stays visible from every page -->
          <span v-if="system.dimmed" class="dim-pill" title="Volume is dimmed">
            <span class="dim-dot"></span>Dimmed
          </span>

          <!-- These modes silently change levels, so they stay visible from
               every page: forgetting one on costs SNR without a symptom -->
          <span
            v-if="compare.enabled || compare.active"
            class="dim-pill"
            title="Comparison mode: A/B states are loudness-matched by trimming the louder one"
          >
            <span class="dim-dot"></span>Matched {{ compare.trimDb.toFixed(1) }} dB
          </span>
          <span
            v-if="sweep.enabled"
            class="dim-pill"
            title="Sweep mode: headroom pads frozen at a 12 dB reserve for EQ tuning"
          >
            <span class="dim-dot"></span>Sweep
          </span>
        </div>

        <div class="hidden sm:flex space-x-6 items-center">
          <router-link
            v-for="tab in tabs"
            :key="tab.name"
            :to="tab.to"
            class="nav-link"
            :class="{ 'nav-link-active': isActive(tab) }"
          >
            {{ tab.label }}
          </router-link>
          <button
            class="nav-link flex items-center gap-1.5 cursor-pointer"
            :class="{ 'nav-link-active': generator.expanded }"
            @click="generator.expanded = !generator.expanded"
          >
            Generator
            <span v-if="generator.isActive" class="w-1.5 h-1.5 rounded-full bg-vybes-live"></span>
          </button>
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
      <!-- In-flow spacer matching the fixed dock's height, so the page
           bottom can scroll clear of it (collapses to 0 when hidden) -->
      <div :style="{ height: dockClearance }" aria-hidden="true"></div>
    </main>

    <!-- Signal generator dock: opened via the Generator nav item, stays
         visible from every page while a generator runs -->
    <GeneratorDock />

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
        <button
          class="flex flex-col items-center gap-0.5 py-2 text-[11px] font-medium transition-colors cursor-pointer"
          :class="generator.expanded ? 'text-vybes-primary' : 'text-vybes-text-secondary'"
          @click="generator.expanded = !generator.expanded"
        >
          <span class="relative">
            <svg class="w-6 h-6" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.8" d="M3 12c1.5-5 3-5 4.5 0s3 5 4.5 0 3-5 4.5 0 3 5 4.5 0" />
            </svg>
            <span v-if="generator.isActive" class="absolute -top-0.5 -right-1 w-1.5 h-1.5 rounded-full bg-vybes-live"></span>
          </span>
          Generator
        </button>
      </div>
    </nav>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue';
import { useRoute } from 'vue-router';
import apiClient from './api-client.js';
import { useSystemStore } from './stores/system.js';
import { useGeneratorStore } from './stores/generator.js';
import { useCompareStore } from './stores/compare.js';
import { useSweepStore } from './stores/sweep.js';
import GeneratorDock from './components/GeneratorDock.vue';

const route = useRoute();
const system = useSystemStore();
const generator = useGeneratorStore();
const compare = useCompareStore();
const sweep = useSweepStore();

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
    name: 'Analyzer',
    label: 'Analyzer',
    to: '/analyzer',
    matches: ['Analyzer'],
    icon: 'M3 13.125C3 12.504 3.504 12 4.125 12h2.25c.621 0 1.125.504 1.125 1.125v6.75C7.5 20.496 6.996 21 6.375 21h-2.25A1.125 1.125 0 013 19.875v-6.75zM9.75 8.625c0-.621.504-1.125 1.125-1.125h2.25c.621 0 1.125.504 1.125 1.125v11.25c0 .621-.504 1.125-1.125 1.125h-2.25a1.125 1.125 0 01-1.125-1.125V8.625zM16.5 4.125c0-.621.504-1.125 1.125-1.125h2.25C20.496 3 21 3.504 21 4.125v15.75c0 .621-.504 1.125-1.125 1.125h-2.25a1.125 1.125 0 01-1.125-1.125V4.125z'
  }
];

const isActive = (tab) => tab.matches.includes(route.name);

// Dock height + a 2rem gap (which also absorbs the dock's own bottom
// offset), so the last card can scroll fully above the dock.
const dockClearance = computed(() =>
  generator.dockHeight ? `${generator.dockHeight + 32}px` : '0px'
);

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
  system.connect();
  generator.connect();
});

onUnmounted(() => {
  if (unsubscribeStatus) unsubscribeStatus();
  system.disconnect();
  generator.disconnect();
});
</script>

<style scoped>
@reference "./style.css";

.dim-pill {
  @apply flex-none flex items-center gap-1.5 rounded-full px-2 py-0.5
         text-[11px] sm:text-xs font-semibold uppercase tracking-wide
         bg-vybes-accent/15 text-vybes-accent border border-vybes-accent/40;
}

.dim-dot {
  @apply w-1.5 h-1.5 rounded-full bg-vybes-accent;
}

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
