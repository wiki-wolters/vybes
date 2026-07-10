<template>
  <div class="container mx-auto px-0 sm:px-4 py-3">
    <h1 class="text-2xl font-semibold mb-6 px-3 sm:px-0 text-vybes-text-primary">Analyzer</h1>

    <!-- Mic unavailable (usually a non-HTTPS origin) -->
    <div
      v-if="!micSupported"
      class="mb-6 mx-3 sm:mx-0 p-4 rounded-md text-sm bg-amber-500/15 border border-amber-500/30 text-amber-200"
    >
      <p class="font-semibold mb-1">Microphone unavailable in this browser</p>
      <p>
        Browsers only allow microphone access on secure origins (HTTPS or localhost), and the
        Vybes UI is served over plain HTTP. The device spectrum below still works. To use the mic
        overlay: on a laptop, run Chrome with
        <code class="text-xs">chrome://flags/#unsafely-treat-insecure-origin-as-secure</code>
        set to this address — iOS Safari has no equivalent override.
      </p>
    </div>

    <div>
      <CardSection title="Spectrum">
        <!-- Legend / status -->
        <div class="flex flex-wrap items-center gap-x-5 gap-y-1 mb-3 text-xs">
          <span class="flex items-center gap-1.5">
            <span class="inline-block w-3 h-0.5 rounded" style="background: #0088ff"></span>
            <span class="text-vybes-text-secondary">Source</span>
            <span :class="sourceLive ? 'text-green-400' : 'text-vybes-text-secondary'">
              {{ sourceLive ? 'live' : 'waiting for device…' }}
            </span>
          </span>
          <span class="flex items-center gap-1.5">
            <span class="inline-block w-3 h-0.5 rounded" style="background: #22c55e"></span>
            <span class="text-vybes-text-secondary">Microphone</span>
            <span :class="micActive ? 'text-green-400' : 'text-vybes-text-secondary'">
              {{ micActive ? (calCurve ? 'on (calibrated)' : 'on') : 'off' }}
            </span>
          </span>
          <span v-if="micActive && sourceLive" class="text-vybes-text-secondary">
            level aligned {{ offset >= 0 ? '+' : '' }}{{ offset.toFixed(1) }} dB
          </span>
        </div>

        <!-- Overlay chart -->
        <div ref="chartContainer" class="w-full rounded bg-black/30 overflow-hidden">
          <svg :width="width" :height="chartHeight" class="block">
            <!-- dB gridlines -->
            <g v-for="line in dbGridLines" :key="'db' + line.db">
              <line :x1="padLeft" :y1="line.y" :x2="width" :y2="line.y" stroke="#333" stroke-width="1" opacity="0.5" />
              <text :x="4" :y="line.y + 3" fill="#666" font-size="9">{{ line.db }}</text>
            </g>
            <!-- frequency gridlines -->
            <g v-for="line in freqGridLines" :key="'f' + line.label">
              <line :x1="line.x" :y1="0" :x2="line.x" :y2="chartHeight - 14" stroke="#333" stroke-width="1" opacity="0.5" />
              <text :x="line.x" :y="chartHeight - 4" fill="#666" font-size="9" text-anchor="middle">{{ line.label }}</text>
            </g>
            <path v-if="sourcePath" :d="sourcePath" fill="none" stroke="#0088ff" stroke-width="2" opacity="0.9" />
            <path v-if="micPath" :d="micPath" fill="none" stroke="#22c55e" stroke-width="2" opacity="0.9" />
          </svg>
        </div>

        <!-- Delta chart: mic minus source, level-aligned -->
        <div v-if="micActive && sourceLive" class="mt-4">
          <p class="text-xs text-vybes-text-secondary mb-2">
            Room + system deviation (mic − source). Bars above zero are frequencies the room/system
            boosts; below zero, frequencies it loses.
          </p>
          <div class="w-full rounded bg-black/30 overflow-hidden">
            <svg :width="width" :height="deltaHeight" class="block">
              <g v-for="line in deltaGridLines" :key="'d' + line.db">
                <line :x1="padLeft" :y1="line.y" :x2="width" :y2="line.y" stroke="#333" stroke-width="1" opacity="0.5" />
                <text :x="4" :y="line.y + 3" fill="#666" font-size="9">{{ line.db > 0 ? '+' + line.db : line.db }}</text>
              </g>
              <line :x1="padLeft" :y1="deltaZeroY" :x2="width" :y2="deltaZeroY" stroke="#555" stroke-width="1.5" />
              <rect
                v-for="bar in deltaBars"
                :key="'bar' + bar.index"
                :x="bar.x"
                :y="bar.y"
                :width="bar.w"
                :height="bar.h"
                :fill="bar.color"
                :opacity="bar.opacity"
                rx="1"
              />
            </svg>
          </div>
        </div>
        <p v-else class="mt-4 text-xs text-vybes-text-secondary">
          Start the microphone while music (ideally
          <router-link to="/tools" class="text-vybes-accent underline">pink noise</router-link>)
          is playing to see where the room and system deviate from the source.
        </p>
      </CardSection>

      <CardSection title="Controls">
        <div class="grid sm:grid-cols-2 gap-6">
          <div>
            <button
              class="w-full"
              :class="micActive ? 'btn-danger' : 'btn-primary'"
              :disabled="!micSupported"
              @click="toggleMic"
            >
              {{ micActive ? 'Stop Microphone' : 'Start Microphone' }}
            </button>
            <p v-if="micError" class="mt-2 text-xs text-red-400">{{ micError }}</p>

            <button class="w-full mt-3 btn-secondary" @click="frozen = !frozen">
              {{ frozen ? 'Resume' : 'Freeze' }}
            </button>
          </div>

          <div>
            <SelectGroup v-model="averagingSeconds" label="Averaging">
              <option
                v-for="opt in averagingOptions"
                :key="opt.value"
                :value="opt.value"
              >
                {{ opt.label }}
              </option>
            </SelectGroup>
            <p class="mt-1 text-xs text-vybes-text-secondary">
              Longer averaging smooths the traces and cancels out the small timing difference
              between the device tap and the microphone.
            </p>
          </div>
        </div>

        <div class="mt-6 pt-4 border-t border-vybes-dark-input">
          <p class="text-sm font-medium mb-2">Microphone calibration</p>
          <div class="flex flex-wrap items-center gap-3">
            <label class="btn-secondary cursor-pointer">
              Import cal file
              <input type="file" accept=".txt,.cal,.frd,.csv" class="hidden" @change="onCalFileSelected" />
            </label>
            <template v-if="calCurve">
              <span class="text-xs text-green-400">{{ calName }}</span>
              <button class="text-xs text-vybes-text-secondary underline" @click="clearCal">remove</button>
            </template>
            <span v-else class="text-xs text-vybes-text-secondary">
              REW-style text file (“frequency gain” per line). Applied to the mic trace.
            </span>
          </div>
          <p v-if="calError" class="mt-2 text-xs text-red-400">{{ calError }}</p>
        </div>
      </CardSection>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue';
import apiClient from '../api-client.js';
import CardSection from '../components/shared/CardSection.vue';
import SelectGroup from '../components/shared/SelectGroup.vue';
import {
  RTA_BAND_CENTERS,
  RTA_NUM_BANDS,
  decodeRtaFrame,
  bandsFromFFT,
  parseCalibrationFile,
  medianOffset,
} from '../rta.js';

const CAL_STORAGE_KEY = 'vybes-rta-mic-cal';
const KEEPALIVE_INTERVAL_MS = 2000;
const MIC_POLL_INTERVAL_MS = 100;
const SOURCE_STALE_MS = 2500;

// --- Chart layout ---
const chartContainer = ref(null);
// Conservative default so the SVG can never force its own container wider
// than the viewport before the first real measurement.
const width = ref(320);
const chartHeight = 300;
const deltaHeight = 160;
const padLeft = 28;
const DELTA_RANGE_DB = 20;

// --- Live state ---
const sourceDb = ref(null); // Float32Array of averaged dB, or null
const micDb = ref(null);
const nowTick = ref(Date.now()); // refreshed by the poll timer for staleness checks
const lastSourceFrameAt = ref(0);
const micActive = ref(false);
const micError = ref('');
const frozen = ref(false);
const averagingSeconds = ref(2);
const offset = ref(0);

const calCurve = ref(null);
const calName = ref('');
const calError = ref('');

const averagingOptions = [
  { value: 0.5, label: '0.5 s (fast)' },
  { value: 1, label: '1 s' },
  { value: 2, label: '2 s' },
  { value: 4, label: '4 s' },
  { value: 8, label: '8 s (smooth)' },
];

const micSupported = typeof navigator !== 'undefined' && !!navigator.mediaDevices?.getUserMedia;
const sourceLive = computed(
  () => sourceDb.value && nowTick.value - lastSourceFrameAt.value < SOURCE_STALE_MS
);

// --- Exponential averaging in the power domain ---
// avg <- avg + alpha * (new - avg), alpha derived from elapsed time and the
// selected time constant, computed on power (not dB) so loud moments don't
// dominate the way dB-domain averaging would.
function emaUpdate(avgPower, newDb, dtMs) {
  // SelectGroup emits strings, hence the Number()
  const alpha = Math.min(1, dtMs / 1000 / Number(averagingSeconds.value));
  for (let i = 0; i < RTA_NUM_BANDS; i++) {
    const p = Math.pow(10, newDb[i] / 10);
    avgPower[i] = avgPower[i] <= 0 ? p : avgPower[i] + alpha * (p - avgPower[i]);
  }
}

function powerToDb(avgPower) {
  const out = new Float32Array(RTA_NUM_BANDS);
  for (let i = 0; i < RTA_NUM_BANDS; i++) {
    out[i] = avgPower[i] > 1e-12 ? 10 * Math.log10(avgPower[i]) : -120;
  }
  return out;
}

// --- Source (device) spectrum over the live-updates websocket ---
let unsubscribeLive = null;
let keepaliveTimer = null;
let sourceAvgPower = new Float32Array(RTA_NUM_BANDS);
let lastSourceEmaAt = 0;

function onLiveMessage(data) {
  if (!data || data.type !== 'rta' || typeof data.d !== 'string') return;
  const frame = decodeRtaFrame(data.d);
  if (!frame) return;
  const now = Date.now();
  lastSourceFrameAt.value = now;
  if (frozen.value) return;
  emaUpdate(sourceAvgPower, frame, lastSourceEmaAt ? now - lastSourceEmaAt : 1000);
  lastSourceEmaAt = now;
  sourceDb.value = powerToDb(sourceAvgPower);
}

// --- Microphone spectrum via Web Audio ---
let micStream = null;
let audioContext = null;
let analyser = null;
let micPollTimer = null;
let micFreqData = null;
let micAvgPower = new Float32Array(RTA_NUM_BANDS);
let lastMicEmaAt = 0;

async function startMic() {
  micError.value = '';
  try {
    // Ask the browser for a raw capture; processing like AGC or noise
    // suppression would reshape the very spectrum we're measuring.
    micStream = await navigator.mediaDevices.getUserMedia({
      audio: {
        echoCancellation: false,
        noiseSuppression: false,
        autoGainControl: false,
      },
      video: false,
    });
  } catch (err) {
    micError.value =
      err.name === 'NotAllowedError'
        ? 'Microphone permission denied.'
        : `Could not open microphone: ${err.message}`;
    return;
  }

  audioContext = new (window.AudioContext || window.webkitAudioContext)();
  await audioContext.resume();
  analyser = audioContext.createAnalyser();
  analyser.fftSize = 8192;
  analyser.smoothingTimeConstant = 0; // we do our own time averaging
  audioContext.createMediaStreamSource(micStream).connect(analyser);
  micFreqData = new Float32Array(analyser.frequencyBinCount);
  micAvgPower = new Float32Array(RTA_NUM_BANDS);
  lastMicEmaAt = 0;
  micActive.value = true;
}

function stopMic() {
  if (micStream) {
    micStream.getTracks().forEach((t) => t.stop());
    micStream = null;
  }
  if (audioContext) {
    audioContext.close();
    audioContext = null;
  }
  analyser = null;
  micActive.value = false;
  micDb.value = null;
}

function toggleMic() {
  if (micActive.value) stopMic();
  else startMic();
}

// Poll timer: sample the mic FFT, refresh staleness, recompute alignment
function pollTick() {
  nowTick.value = Date.now();
  if (!analyser || frozen.value) return;

  analyser.getFloatFrequencyData(micFreqData);
  const binWidth = audioContext.sampleRate / analyser.fftSize;
  const bands = bandsFromFFT(micFreqData, binWidth);
  if (calCurve.value) {
    for (let i = 0; i < RTA_NUM_BANDS; i++) bands[i] -= calCurve.value[i];
  }
  const now = Date.now();
  emaUpdate(micAvgPower, bands, lastMicEmaAt ? now - lastMicEmaAt : 1000);
  lastMicEmaAt = now;
  micDb.value = powerToDb(micAvgPower);

  // Level alignment: mic and source have unrelated absolute scales, so
  // shift the mic trace by the median mid-band difference before comparing.
  if (sourceDb.value && sourceLive.value) {
    offset.value = medianOffset(micDb.value, sourceDb.value);
  }
}

// --- Calibration file handling ---
function onCalFileSelected(event) {
  calError.value = '';
  const file = event.target.files?.[0];
  event.target.value = '';
  if (!file) return;
  const reader = new FileReader();
  reader.onload = () => {
    const curve = parseCalibrationFile(String(reader.result));
    if (!curve) {
      calError.value = 'No “frequency gain” pairs found in that file.';
      return;
    }
    calCurve.value = curve;
    calName.value = file.name;
    try {
      localStorage.setItem(CAL_STORAGE_KEY, JSON.stringify({ name: file.name, curve }));
    } catch (e) { /* storage full or blocked - cal still applies this session */ }
  };
  reader.readAsText(file);
}

function clearCal() {
  calCurve.value = null;
  calName.value = '';
  localStorage.removeItem(CAL_STORAGE_KEY);
}

// --- Chart geometry ---
const bandX = (i) => padLeft + ((i + 0.5) * (width.value - padLeft)) / RTA_NUM_BANDS;

// Y scale of the overlay chart: 70dB window that tracks the loudest band.
const topDb = computed(() => {
  let max = -60;
  if (sourceDb.value) for (const v of sourceDb.value) max = Math.max(max, v);
  if (micDb.value && sourceLive.value) {
    for (const v of micDb.value) max = Math.max(max, v - offset.value);
  }
  return Math.ceil(max / 10) * 10 + 5;
});
const bottomDb = computed(() => topDb.value - 70);
const dbToY = (db) =>
  ((topDb.value - db) / (topDb.value - bottomDb.value)) * (chartHeight - 14);

const dbGridLines = computed(() => {
  const lines = [];
  for (let db = Math.floor(topDb.value / 10) * 10; db >= bottomDb.value; db -= 10) {
    lines.push({ db, y: dbToY(db) });
  }
  return lines;
});

const freqGridLines = computed(() => {
  const marks = { 31.5: '31', 63: '63', 125: '125', 250: '250', 500: '500', 1000: '1k', 2000: '2k', 4000: '4k', 8000: '8k', 16000: '16k' };
  return RTA_BAND_CENTERS.filter((fc) => marks[fc]).map((fc) => ({
    label: marks[fc],
    x: bandX(RTA_BAND_CENTERS.indexOf(fc)),
  }));
});

function tracePath(values, shift = 0) {
  const points = [];
  for (let i = 0; i < RTA_NUM_BANDS; i++) {
    const db = values[i] - shift;
    if (db <= -110) continue; // don't draw the floor
    points.push(`${bandX(i).toFixed(1)},${Math.min(chartHeight - 14, Math.max(0, dbToY(db))).toFixed(1)}`);
  }
  return points.length > 1 ? `M ${points.join(' L ')}` : '';
}

const sourcePath = computed(() => (sourceDb.value ? tracePath(sourceDb.value) : ''));
// The mic trace is drawn pre-shifted onto the source's scale
const micPath = computed(() => (micDb.value ? tracePath(micDb.value, sourceLive.value ? offset.value : 0) : ''));

// --- Delta chart ---
const deltaZeroY = deltaHeight / 2;
const deltaDbToY = (db) => deltaZeroY - (db / DELTA_RANGE_DB) * (deltaHeight / 2 - 8);

const deltaGridLines = computed(() =>
  [-20, -10, 10, 20].map((db) => ({ db, y: deltaDbToY(db) }))
);

const deltaBars = computed(() => {
  if (!micDb.value || !sourceDb.value || !sourceLive.value) return [];
  const bars = [];
  const bw = ((width.value - padLeft) / RTA_NUM_BANDS) * 0.66;
  for (let i = 0; i < RTA_NUM_BANDS; i++) {
    // Skip bands where the source has effectively no content - the delta
    // there is just noise-floor arithmetic, not room response.
    if (sourceDb.value[i] <= -85 || micDb.value[i] <= -110) continue;
    const d = Math.max(-DELTA_RANGE_DB, Math.min(DELTA_RANGE_DB, micDb.value[i] - offset.value - sourceDb.value[i]));
    const y0 = deltaDbToY(Math.max(0, d));
    bars.push({
      index: i,
      x: bandX(i) - bw / 2,
      y: y0,
      w: bw,
      h: Math.max(1, Math.abs(deltaDbToY(d) - deltaZeroY)),
      color: d >= 0 ? '#f59e0b' : '#38bdf8',
      opacity: Math.abs(d) < 2 ? 0.35 : 0.9,
    });
  }
  return bars;
});

// --- Lifecycle ---
let resizeObserver = null;

onMounted(() => {
  // Stored calibration survives reloads
  try {
    const stored = JSON.parse(localStorage.getItem(CAL_STORAGE_KEY));
    if (stored?.curve?.length === RTA_NUM_BANDS) {
      calCurve.value = stored.curve;
      calName.value = stored.name || 'stored calibration';
    }
  } catch (e) { /* ignore corrupt storage */ }

  unsubscribeLive = apiClient.connectLiveUpdates(onLiveMessage);

  // Keepalive: tells the device to stream RTA frames while this page is open
  apiClient.sendLiveMessage('rta:keepalive');
  keepaliveTimer = setInterval(
    () => apiClient.sendLiveMessage('rta:keepalive'),
    KEEPALIVE_INTERVAL_MS
  );

  micPollTimer = setInterval(pollTick, MIC_POLL_INTERVAL_MS);

  const updateWidth = () => {
    if (chartContainer.value?.clientWidth > 0) width.value = chartContainer.value.clientWidth;
  };
  updateWidth();
  if (window.ResizeObserver && chartContainer.value) {
    resizeObserver = new ResizeObserver(updateWidth);
    resizeObserver.observe(chartContainer.value);
  }
});

onUnmounted(() => {
  if (unsubscribeLive) unsubscribeLive();
  clearInterval(keepaliveTimer);
  clearInterval(micPollTimer);
  if (resizeObserver) resizeObserver.disconnect();
  stopMic();
});
</script>
