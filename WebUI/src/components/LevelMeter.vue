<template>
  <!-- Stereo peak meter for the input bus: the summing point after the
       input gain sliders (and the SD player), i.e. what the recorder
       captures and where stacked sources clip. Fed by 20Hz "vu" frames
       while mounted; green -> orange near full scale, and the LED lights
       red only on an actual clipped (flat-topped) waveform. -->
  <div class="space-y-1.5" role="img" aria-label="Input level meter">
    <div v-for="ch in 2" :key="ch" class="flex items-center gap-2">
      <span class="w-3 flex-none text-xs font-medium text-vybes-text-secondary">
        {{ ch === 1 ? 'L' : 'R' }}
      </span>
      <div class="meter-track">
        <!-- fixed gradient scale, revealed by the level -->
        <div class="meter-scale"></div>
        <div class="meter-cover" :style="{ width: coverWidth(ch - 1) }"></div>
        <div v-if="peakHold[ch - 1] > 0" class="meter-peak" :style="{ left: peakLeft(ch - 1) }"></div>
      </div>
      <span
        class="clip-led"
        :class="clipLit[ch - 1] ? 'clip-led-on' : ''"
        :title="clipLit[ch - 1] ? 'Clipping!' : 'No clipping'"
      ></span>
    </div>
    <div class="flex items-center gap-2 text-[10px] text-vybes-text-secondary tabular-nums select-none">
      <span class="w-3 flex-none"></span>
      <div class="flex-1 flex justify-between px-0.5">
        <span>-60</span><span>-40</span><span>-20</span><span>-12</span><span>-3</span><span>0 dB</span>
      </div>
      <span class="w-2.5 flex-none"></span>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue';
import apiClient from '../api-client.js';

// Scale: byte 0..255 maps dBFS -60..0 linearly (matches the firmware's
// encoding), rendered as 0..100% of the track.
const levelPct = ref([0, 0]);   // current fill
const peakHold = ref([0, 0]);   // decaying peak-hold tick, in %
const clipLit = ref([false, false]);

const CLIP_HOLD_MS = 1500;
const PEAK_DECAY_PCT_PER_FRAME = 0.9; // ~18%/s at 20Hz

let clipTimers = [null, null];
let unsubscribeLive = null;
let keepaliveTimer = null;

function coverWidth(i) {
  return `${(100 - levelPct.value[i]).toFixed(1)}%`;
}

function peakLeft(i) {
  return `${peakHold.value[i].toFixed(1)}%`;
}

function onFrame(d) {
  if (typeof d !== 'string' || d.length < 5) return;
  const bytes = [parseInt(d.slice(0, 2), 16), parseInt(d.slice(2, 4), 16)];
  const flags = parseInt(d.slice(4, 5), 16);
  for (let i = 0; i < 2; i++) {
    if (Number.isNaN(bytes[i])) continue;
    const pct = (bytes[i] / 255) * 100;
    levelPct.value[i] = pct;
    peakHold.value[i] = Math.max(pct, peakHold.value[i] - PEAK_DECAY_PCT_PER_FRAME);
    if (flags & (1 << i)) {
      clipLit.value[i] = true;
      clearTimeout(clipTimers[i]);
      clipTimers[i] = setTimeout(() => { clipLit.value[i] = false; }, CLIP_HOLD_MS);
    }
  }
}

onMounted(() => {
  unsubscribeLive = apiClient.connectLiveUpdates((data) => {
    if (data?.type === 'vu') onFrame(data.d);
  });
  apiClient.sendLiveMessage('vu:keepalive');
  keepaliveTimer = setInterval(() => apiClient.sendLiveMessage('vu:keepalive'), 2000);
});

onUnmounted(() => {
  if (unsubscribeLive) unsubscribeLive();
  clearInterval(keepaliveTimer);
  clipTimers.forEach((t) => clearTimeout(t));
});
</script>

<style scoped>
@reference '../style.css';

.meter-track {
  @apply relative flex-1 h-2.5 rounded-sm overflow-hidden bg-vybes-dark-input;
}

/* Desk-style zones: green to -12dBFS (80%), orange to -3dBFS (95%), red top */
.meter-scale {
  @apply absolute inset-0;
  background: linear-gradient(
    to right,
    #16a34a 0%, #22c55e 80%,
    #f59e0b 80%, #f59e0b 95%,
    #ef4444 95%, #ef4444 100%
  );
}

.meter-cover {
  @apply absolute inset-y-0 right-0 bg-vybes-dark-input;
  transition: width 60ms linear;
}

.meter-peak {
  @apply absolute inset-y-0 w-px bg-white/80;
}

.clip-led {
  @apply flex-none w-2.5 h-2.5 rounded-full bg-vybes-dark-input border border-vybes-border;
}

.clip-led-on {
  @apply bg-red-500 border-red-400;
  box-shadow: 0 0 6px 1px rgb(239 68 68 / 0.7);
}
</style>
