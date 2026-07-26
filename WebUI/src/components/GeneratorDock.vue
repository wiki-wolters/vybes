<template>
  <!-- Above the mobile tab bar (z-40); bottom-right corner on desktop.
       Opened via the Generator nav item; hidden entirely when idle. -->
  <div
    v-if="gen.expanded || gen.isActive"
    ref="dockEl"
    class="fixed z-30 pointer-events-none inset-x-0 bottom-[calc(3.5rem+env(safe-area-inset-bottom))] sm:inset-x-auto sm:right-4 sm:bottom-4 sm:w-80"
  >
    <!-- Expanded: full controls -->
    <div v-if="gen.expanded" class="dock-surface pointer-events-auto mx-2 sm:mx-0 p-4">
      <div class="flex items-center gap-2 mb-3">
        <span class="live-dot" :class="gen.isActive ? 'bg-vybes-live' : 'bg-vybes-text-secondary/40'"></span>
        <span class="text-sm font-medium text-vybes-text-primary">Signal generator</span>
        <button class="btn-icon ml-auto -my-2 -mr-2 hover:bg-vybes-dark-input" aria-label="Collapse" @click="gen.expanded = false">
          <svg class="w-5 h-5 text-vybes-text-secondary" fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" d="M19.5 8.25l-7.5 7.5-7.5-7.5" />
          </svg>
        </button>
      </div>

      <div class="grid grid-cols-2 rounded-md border border-vybes-border-solid overflow-hidden mb-4">
        <button
          v-for="opt in sources"
          :key="opt.id"
          class="py-1.5 text-sm font-medium transition-colors cursor-pointer"
          :class="gen.source === opt.id
            ? 'bg-vybes-primary/20 text-vybes-primary'
            : 'text-vybes-text-secondary hover:text-vybes-text-primary'"
          @click="gen.setSource(opt.id)"
        >
          {{ opt.label }}
        </button>
      </div>

      <div v-if="gen.source === 'tone'" class="mb-4">
        <RangeSlider
          v-model="gen.toneFrequency"
          label="Frequency — drag to sweep"
          :min="20"
          :max="20000"
          :decimals="0"
          unit="Hz"
          logarithmic
        />
      </div>

      <div class="mb-4">
        <RangeSlider
          v-model="volumeModel"
          label="Volume"
          :min="1"
          :max="100"
          :decimals="0"
          unit="%"
        />
      </div>

      <button class="w-full" :class="gen.isActive ? 'btn-danger' : 'btn-primary'" @click="gen.toggle()">
        {{ gen.isActive ? 'Stop' : `Start ${gen.source === 'tone' ? 'tone' : 'pink noise'}` }}
      </button>
      <p v-if="gen.error" class="mt-2 text-xs text-red-400">{{ gen.error }}</p>
    </div>

    <!-- Collapsed + running: slim bar with quick volume -->
    <div
      v-else
      class="dock-surface pointer-events-auto mx-2 sm:mx-0 px-3 py-2 flex items-center gap-3"
    >
      <span class="live-dot bg-vybes-live"></span>
      <span class="text-sm text-vybes-text-primary whitespace-nowrap tabular-nums">{{ gen.statusLabel }}</span>
      <input
        v-model.number="volumeModel"
        type="range"
        min="1"
        max="100"
        step="1"
        aria-label="Generator volume"
        class="mini-slider flex-1 min-w-14"
      />
      <button
        class="text-xs font-semibold px-2.5 py-1 rounded-md bg-red-600/15 text-red-400 hover:bg-red-600/30 transition-colors cursor-pointer"
        @click="gen.stop()"
      >
        Stop
      </button>
      <button class="btn-icon -my-1 -mr-1.5 hover:bg-vybes-dark-input" aria-label="Expand" @click="gen.expanded = true">
        <svg class="w-5 h-5 text-vybes-text-secondary" fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24">
          <path stroke-linecap="round" stroke-linejoin="round" d="M4.5 15.75l7.5-7.5 7.5 7.5" />
        </svg>
      </button>
    </div>
  </div>
</template>

<script setup>
import { computed, ref, watch, onUpdated, onUnmounted } from 'vue';
import { useGeneratorStore } from '../stores/generator.js';
import RangeSlider from './shared/RangeSlider.vue';

const gen = useGeneratorStore();

// Report the rendered height so App.vue can pad the page bottom and let
// content scroll clear of the dock. Height changes come from this
// component's own re-renders (expand/collapse, source switch, error text),
// so onUpdated catches them; the ResizeObserver covers the rest (viewport
// resizes, font loading). The root is v-if'd, so the template ref flips
// between an element and null.
const dockEl = ref(null);
const reportHeight = () => { gen.dockHeight = dockEl.value?.offsetHeight ?? 0; };
const resizeObserver = typeof ResizeObserver !== 'undefined'
  ? new ResizeObserver(reportHeight)
  : null;

watch(dockEl, (el) => {
  resizeObserver?.disconnect();
  if (el) resizeObserver?.observe(el);
  reportHeight();
});

onUpdated(reportHeight);

onUnmounted(() => {
  resizeObserver?.disconnect();
  gen.dockHeight = 0;
});

const sources = [
  { id: 'noise', label: 'Pink noise' },
  { id: 'tone', label: 'Tone' },
];

// The volume slider always edits the source the dock is showing; the store
// remembers tone and noise levels separately.
const volumeModel = computed({
  get: () => (gen.source === 'tone' ? gen.toneVolume : gen.noiseVolume),
  set: (v) => {
    if (gen.source === 'tone') gen.toneVolume = v;
    else gen.noiseVolume = v;
  },
});
</script>

<style scoped>
@reference "../style.css";

/* Floating layer, deliberately distinct from the page's cards: lighter
   translucent surface with blur (content visibly scrolls beneath it) and
   an amber-tinted border — amber = "currently in effect". */
.dock-surface {
  @apply rounded-lg border border-vybes-accent/40 bg-vybes-dark-float/90 backdrop-blur-md
         shadow-xl shadow-black/60;
}

.live-dot {
  @apply flex-none w-2 h-2 rounded-full;
}

/* Compact cousin of RangeSlider's track/thumb for the collapsed bar */
.mini-slider {
  @apply h-1.5 bg-vybes-dark-input rounded-lg appearance-none cursor-pointer select-none
         focus:outline-none focus:ring-2 focus:ring-vybes-blue/50;
  touch-action: pan-y;
}

.mini-slider::-webkit-slider-thumb {
  @apply appearance-none w-4 h-4 bg-vybes-blue rounded-full shadow;
}

.mini-slider::-moz-range-thumb {
  @apply w-4 h-4 bg-vybes-blue rounded-full border-none shadow;
}

.mini-slider::-moz-range-track {
  background-color: var(--vybes-dark-input);
  height: 0.375rem;
  border-radius: 0.5rem;
}
</style>
