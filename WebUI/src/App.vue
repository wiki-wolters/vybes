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
          <!-- Wordmark inlined (small cut, currentColor) so the brand costs no
               extra request — the device's TLS sockets are scarce. Teal is the
               brand colour; amber now only ever means "in effect". -->
          <router-link
            to="/"
            class="flex-none flex items-center text-vybes-brand hover:text-vybes-brand-light transition-colors"
            aria-label="Vybes — home"
          >
            <svg class="h-6 sm:h-7 w-auto" viewBox="0 0 3616 1161" fill="currentColor" aria-hidden="true">
              <path fill-rule="evenodd" d="M809.7 978.4L803.7 1008.8L791.8 1037.4L774.6 1063.1L752.7 1085.0L726.9 1102.3L698.3 1114.1L668.0 1120.2L637.0 1120.2L606.6 1114.1L578.0 1102.3L552.2 1085.0L530.3 1063.1L513.1 1037.4L501.3 1008.8L495.2 978.4L495.2 947.4L501.3 917.0L513.1 888.4L530.3 862.7L552.2 840.8L578.0 823.6L606.6 811.7L637.0 805.7L668.0 805.7L698.3 811.7L726.9 823.6L752.7 840.8L774.6 862.7L791.8 888.4L803.7 917.0L809.7 947.4L809.7 978.4ZM1404.5 793.9L1182.5 253.9L1339.5 253.9L1482.5 640.9L1627.5 253.9L1776.5 253.9L1433.5 1069.9L1284.5 1069.9L1404.5 793.9ZM2638.5 594.5L2643.7 614.3L2651.2 632.2L2661.0 648.3L2677.5 666.9L2691.9 678.5L2713.1 690.2L2730.4 696.1L2755.2 700.4L2778.0 700.7L2796.1 698.6L2812.8 694.4L2828.1 688.1L2842.0 679.7L2854.4 669.2L2865.5 656.7L2879.5 633.9L3030.5 633.9L3023.8 653.5L3015.7 672.1L3006.3 689.9L2989.5 714.9L2976.7 730.4L2954.8 752.1L2938.5 765.4L2921.0 777.5L2902.5 787.9L2883.1 796.8L2862.7 804.0L2841.4 809.7L2819.0 813.7L2795.7 816.1L2751.7 816.4L2713.8 811.9L2695.6 808.0L2669.3 800.1L2652.4 793.5L2628.0 781.4L2598.0 761.8L2571.5 738.5L2559.5 725.5L2543.3 704.3L2529.0 680.9L2520.6 664.4L2507.3 629.4L2498.5 591.9L2494.0 551.9L2493.6 520.2L2494.7 499.4L2500.2 459.7L2510.0 422.6L2524.2 388.1L2542.6 356.6L2553.1 342.2L2570.5 322.4L2596.7 299.3L2611.2 289.1L2634.4 275.7L2667.7 261.5L2703.4 251.6L2722.1 248.3L2751.3 245.5L2771.5 244.9L2800.5 246.1L2837.4 251.4L2855.1 255.7L2880.7 264.0L2905.1 274.8L2920.7 283.3L2949.3 302.9L2974.4 326.2L2985.7 339.1L3000.9 360.2L3014.1 383.3L3025.1 407.8L3033.6 433.6L3039.6 460.8L3043.3 489.2L3044.5 518.9L3043.5 547.4L3040.5 572.9L2635.5 572.9L2638.5 594.5ZM2896.7 453.3L2891.5 435.8L2880.6 414.8L2865.1 396.6L2845.6 381.4L2829.6 372.6L2806.3 364.5L2781.0 360.4L2754.7 360.4L2736.6 363.0L2714.2 369.9L2693.9 380.7L2675.6 395.4L2663.9 408.7L2651.3 429.0L2642.2 452.4L2636.5 478.9L2899.5 478.9L2896.7 453.3ZM3257.5 632.9L3262.3 651.8L3269.4 665.4L3276.7 674.7L3289.0 685.9L3299.8 693.0L3315.6 700.3L3328.6 704.0L3347.4 706.6L3367.2 706.7L3385.0 704.7L3400.6 700.7L3414.1 694.7L3425.2 686.8L3435.3 675.2L3440.3 664.7L3442.4 653.0L3441.5 640.3L3437.2 628.9L3429.5 619.2L3418.3 611.2L3399.2 602.5L3373.7 593.4L3271.1 564.7L3226.0 548.9L3211.5 542.3L3193.5 531.7L3176.9 518.9L3161.6 504.0L3148.3 486.7L3138.5 466.6L3133.5 449.7L3129.9 424.6L3129.9 399.5L3133.2 377.5L3139.8 356.5L3149.8 336.5L3163.0 317.7L3174.8 304.7L3188.2 292.9L3203.3 282.2L3225.9 269.7L3244.6 261.8L3264.6 255.3L3285.8 250.4L3308.3 247.1L3340.2 245.0L3372.6 245.7L3395.5 248.0L3417.1 251.9L3437.5 257.3L3456.6 264.2L3482.9 277.6L3498.9 288.4L3520.4 307.3L3532.7 321.3L3547.9 344.3L3556.1 361.0L3562.5 378.8L3567.4 397.6L3571.5 427.9L3437.5 427.9L3434.6 412.3L3429.1 398.3L3420.9 386.0L3410.0 375.4L3400.2 368.8L3385.4 362.1L3368.5 357.8L3349.5 356.0L3330.8 356.4L3314.6 358.6L3300.5 362.5L3286.0 369.9L3277.0 377.6L3269.4 389.1L3265.8 402.8L3265.9 415.7L3268.2 425.1L3274.3 436.1L3281.2 443.3L3293.5 451.4L3309.4 458.7L3334.8 467.9L3428.9 494.2L3479.5 511.9L3493.7 518.5L3511.6 529.3L3528.1 542.2L3543.4 557.3L3556.7 574.7L3566.6 594.8L3573.2 617.6L3576.2 643.1L3575.5 667.6L3571.4 690.0L3563.9 711.2L3553.2 731.1L3543.0 745.2L3531.2 758.1L3517.7 769.8L3502.7 780.4L3486.0 789.8L3467.7 798.0L3434.5 808.5L3413.1 812.8L3390.4 815.6L3350.0 816.8L3309.4 813.5L3286.4 809.2L3264.3 803.1L3236.5 792.4L3204.9 775.4L3182.8 759.3L3159.2 736.1L3140.2 709.8L3128.9 687.4L3121.1 663.9L3116.5 632.9L3257.5 632.9ZM2209.1 247.1L2242.1 253.7L2273.4 264.6L2303.0 279.9L2330.2 299.3L2354.5 322.3L2375.7 349.0L2394.0 379.4L2408.6 412.9L2419.1 449.0L2425.4 487.7L2427.5 528.9L2425.4 570.2L2421.1 599.6L2411.6 636.6L2398.0 671.1L2380.6 702.9L2370.7 717.5L2354.5 737.5L2336.6 755.5L2317.0 771.5L2295.7 785.3L2273.4 796.7L2250.1 805.5L2225.8 811.9L2192.0 816.4L2151.8 816.1L2123.2 812.2L2090.2 803.1L2060.2 789.3L2033.7 772.0L2010.8 751.9L1991.5 728.9L1991.5 807.9L1851.5 807.9L1851.5 67.9L1991.5 67.9L1991.5 334.9L2010.3 311.4L2033.2 290.8L2060.1 273.1L2084.3 261.4L2110.1 252.8L2144.8 246.5L2174.5 244.9L2209.1 247.1ZM2132.7 368.0L2105.1 371.6L2087.3 377.1L2061.8 389.9L2039.1 407.8L2020.1 430.7L2005.0 458.2L1996.6 484.2L1991.8 518.6L1992.7 555.1L1998.0 583.0L2003.0 598.6L2009.5 613.2L2026.0 639.3L2046.3 660.5L2070.2 676.8L2096.1 687.7L2123.4 693.2L2151.9 693.2L2179.6 687.6L2192.9 682.7L2210.0 673.9L2233.3 656.5L2243.5 646.0L2255.8 630.2L2270.9 602.5L2279.3 576.4L2284.1 541.4L2283.2 504.8L2278.0 476.9L2272.9 461.5L2266.4 447.0L2249.9 421.3L2229.6 400.5L2205.8 384.6L2179.6 373.9L2151.9 368.6L2132.7 368.0ZM155.3 47.0L182.0 54.2L205.9 62.7L225.4 71.1L249.8 84.1L269.5 96.8L286.9 109.9L310.8 131.0L327.5 148.3L360.9 189.8L383.0 223.2L398.1 248.8L427.9 306.6L463.3 387.6L504.7 496.9L585.9 728.2L551.8 740.7L516.9 760.0L485.7 784.8L462.0 810.4L435.6 739.0L368.2 546.0L335.3 458.0L315.9 410.8L296.8 368.5L278.1 331.5L260.0 299.9L242.6 273.7L226.0 252.4L210.3 235.6L190.7 218.7L171.9 206.6L148.2 195.8L128.1 189.6L91.3 181.8L78.1 176.3L66.3 168.4L56.3 158.3L48.5 146.5L44.6 137.9L40.3 119.4L40.3 105.2L41.9 95.8L44.7 86.8L51.0 74.0L56.5 66.3L66.6 56.3L78.4 48.5L87.1 44.6L105.5 40.3L124.4 41.0L155.3 47.0ZM719.0 728.2L820.8 441.0L848.7 370.5L869.9 321.9L892.0 276.2L921.1 224.5L944.1 189.8L967.8 159.2L994.2 131.0L1028.7 101.7L1063.9 79.1L1081.5 70.2L1105.8 60.1L1148.2 47.3L1189.9 40.0L1204.1 40.9L1217.9 44.6L1226.5 48.5L1238.3 56.3L1248.4 66.3L1253.9 74.0L1260.3 86.8L1264.0 100.5L1264.9 114.7L1263.1 128.8L1258.6 142.2L1251.5 154.6L1238.6 168.4L1222.5 178.4L1209.1 183.0L1171.7 191.0L1151.9 197.7L1128.4 209.3L1109.5 222.4L1084.2 246.4L1062.3 273.7L1039.0 309.8L1020.7 343.2L989.1 410.8L956.5 492.0L869.2 739.4L842.9 810.4L819.3 784.8L788.0 760.0L753.2 740.7L719.0 728.2Z"/>
            </svg>
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
          :class="isActive(tab) ? 'text-vybes-brand' : 'text-vybes-text-secondary'"
        >
          <svg class="w-6 h-6" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.8" :d="tab.icon" />
          </svg>
          {{ tab.shortLabel ?? tab.label }}
        </router-link>
        <button
          class="flex flex-col items-center gap-0.5 py-2 text-[11px] font-medium transition-colors cursor-pointer"
          :class="generator.expanded ? 'text-vybes-brand' : 'text-vybes-text-secondary'"
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
  color: var(--vybes-brand);
}
</style>
