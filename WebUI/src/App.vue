<template>
  <div id="app" class="min-h-screen bg-vybes-dark text-vybes-text-primary flex flex-col">
    <!-- Top bar: brand always, links on desktop only -->
    <nav class="bg-vybes-dark-element border-b border-vybes-dark-input">
      <div class="container mx-auto px-4 py-3 sm:py-4 flex items-center justify-between gap-3">
        <!-- The status pills come and go with live state, and on a narrow
             phone they squeezed the brand onto two lines: the bar grew and the
             whole page jumped. The brand never wraps and the pills scroll
             instead, so the bar keeps one height whatever is lit. -->
        <div class="flex items-center gap-2 min-w-0 overflow-x-auto nav-status">
          <router-link
            to="/"
            class="flex-none whitespace-nowrap text-xl sm:text-2xl font-bold text-vybes-accent hover:text-vybes-accent-light transition-colors"
          >
            Vybes DSP
          </router-link>

          <!-- Dim is a live state, so it stays visible from every page -->
          <span v-if="system.dimmed" class="dim-pill" title="Volume is dimmed">
            <span class="dim-dot"></span>Dimmed
          </span>

          <!-- Recording to SD: visible everywhere, like the LCD's REC dot -->
          <span
            v-if="recorder.isRecording"
            class="rec-pill tabular-nums"
            title="Recording the stereo input to SD — preset switching is locked"
          >
            <span class="rec-pill-dot animate-pulse"></span>Rec {{ recTime }}
          </span>
        </div>

        <div class="hidden sm:flex space-x-6 items-center">
          <router-link
            v-for="tab in tabs"
            :key="tab.name"
            :to="tab.to"
            class="nav-link max-w-[10rem] truncate"
            :class="{ 'nav-link-active': isActive(tab) }"
            :title="tab.label"
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

    <!-- Device connectivity banner. Overlaid rather than in-flow: it comes and
         goes on its own while the page is in use, and taking layout space made
         the whole page jump each time. -->
    <div
      v-if="showOfflineBanner"
      class="fixed z-50 top-[3.5rem] sm:top-[4.25rem] inset-x-0 flex justify-center px-4 pointer-events-none"
      role="status"
    >
      <div
        class="flex items-center gap-2 rounded-full border border-amber-500/40 bg-vybes-dark-element text-amber-200 text-sm px-4 py-1.5 shadow-lg shadow-black/40"
      >
        <span class="w-1.5 h-1.5 rounded-full bg-amber-400"></span>
        Device offline — reconnecting…
      </div>
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
      <!-- The preset tab drops out until the active preset is known, so the
           column count is computed rather than a fixed Tailwind class -->
      <div class="grid" :style="{ gridTemplateColumns: `repeat(${tabs.length + 1}, minmax(0, 1fr))` }">
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
          {{ tab.shortLabel ?? tab.label }}
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
import { useRecorderStore } from './stores/recorder.js';
import GeneratorDock from './components/GeneratorDock.vue';

const route = useRoute();
const system = useSystemStore();
const generator = useGeneratorStore();
const recorder = useRecorderStore();

const recTime = computed(() => {
  const s = Math.max(0, Math.floor(recorder.recording.seconds ?? 0));
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
});

const HOME_TAB = {
  name: 'Home',
  label: 'Home',
  to: '/',
  matches: ['Home'],
  icon: 'M3 12l2-2m0 0l7-7 7 7M5 10v10a1 1 0 001 1h3m10-11l2 2m-2-2v10a1 1 0 01-1 1h-3m-6 0a1 1 0 001-1v-4a1 1 0 011-1h2a1 1 0 011 1v4a1 1 0 001 1m-6 0h6'
};

const ANALYZER_TAB = {
  name: 'Analyzer',
  label: 'Analyzer',
  to: '/analyzer',
  matches: ['Analyzer'],
  icon: 'M3 13.125C3 12.504 3.504 12 4.125 12h2.25c.621 0 1.125.504 1.125 1.125v6.75C7.5 20.496 6.996 21 6.375 21h-2.25A1.125 1.125 0 013 19.875v-6.75zM9.75 8.625c0-.621.504-1.125 1.125-1.125h2.25c.621 0 1.125.504 1.125 1.125v11.25c0 .621-.504 1.125-1.125 1.125h-2.25a1.125 1.125 0 01-1.125-1.125V8.625zM16.5 4.125c0-.621.504-1.125 1.125-1.125h2.25C20.496 3 21 3.504 21 4.125v15.75c0 .621-.504 1.125-1.125 1.125h-2.25a1.125 1.125 0 01-1.125-1.125V4.125z'
};

// The active preset's editor is its own destination: it used to keep the
// Home tab lit, which left no way to get back to Home except tapping the
// tab that already looked selected. Desktop shows the preset's name, the
// bottom bar the generic label (long names don't fit a phone tab).
const presetTab = computed(() => {
  if (!system.currentPreset) return null;
  return {
    name: 'Preset',
    label: system.currentPreset,
    shortLabel: 'Preset',
    to: `/preset/${encodeURIComponent(system.currentPreset)}`,
    matches: ['Preset'],
    icon: 'M10.5 6h9.75M10.5 6a1.5 1.5 0 11-3 0m3 0a1.5 1.5 0 10-3 0M3.75 6H7.5m3 12h9.75m-9.75 0a1.5 1.5 0 01-3 0m3 0a1.5 1.5 0 00-3 0m-3.75 0H7.5m9-6h3.75m-3.75 0a1.5 1.5 0 01-3 0m3 0a1.5 1.5 0 00-3 0m-9.75 0h9.75'
  };
});

const tabs = computed(() =>
  presetTab.value ? [HOME_TAB, presetTab.value, ANALYZER_TAB] : [HOME_TAB, ANALYZER_TAB]
);

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
  recorder.connect();
});

onUnmounted(() => {
  if (unsubscribeStatus) unsubscribeStatus();
  system.disconnect();
  generator.disconnect();
  recorder.disconnect();
});
</script>

<style scoped>
@reference "./style.css";

/* Swipeable when several pills are lit at once, without a scrollbar
   thickening the bar. */
.nav-status {
  scrollbar-width: none;
}

.nav-status::-webkit-scrollbar {
  display: none;
}

.dim-pill {
  @apply flex-none flex items-center gap-1.5 rounded-full px-2 py-0.5
         text-[11px] sm:text-xs font-semibold uppercase tracking-wide
         bg-vybes-accent/15 text-vybes-accent border border-vybes-accent/40;
}

.dim-dot {
  @apply w-1.5 h-1.5 rounded-full bg-vybes-accent;
}

.rec-pill {
  @apply flex-none flex items-center gap-1.5 rounded-full px-2 py-0.5
         text-[11px] sm:text-xs font-semibold uppercase tracking-wide
         bg-red-500/15 text-red-400 border border-red-500/40;
}

.rec-pill-dot {
  @apply w-1.5 h-1.5 rounded-full bg-red-500;
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
