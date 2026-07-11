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
            <svg
              :width="width"
              :height="deltaHeight"
              class="block cursor-crosshair"
              style="touch-action: pan-y"
              @pointermove="onDeltaHover"
              @pointerdown="onDeltaHover"
              @pointerleave="onDeltaLeave"
            >
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
              <!-- Proposed EQ correction and the predicted result of applying it -->
              <template v-if="frozen && generatedPoints.length">
                <path :d="correctionPath" fill="none" stroke="#e879f9" stroke-width="2" opacity="0.9" />
                <path :d="predictedPath" fill="none" stroke="#e5e7eb" stroke-width="1.5" stroke-dasharray="4 3" opacity="0.75" />
              </template>
              <!-- Crosshair: nearest band under the pointer/finger -->
              <g v-if="deltaHover" pointer-events="none">
                <line :x1="deltaHover.x" :y1="0" :x2="deltaHover.x" :y2="deltaHeight" stroke="#fff" stroke-width="1" opacity="0.5" />
                <circle v-if="deltaHover.hasValue" :cx="deltaHover.x" :cy="deltaHover.y" r="3" fill="#fff" />
                <rect :x="deltaHover.labelX - 46" y="4" width="92" height="28" rx="4" fill="rgba(10, 13, 17, 0.9)" stroke="rgba(148, 168, 196, 0.25)" />
                <text :x="deltaHover.labelX" y="16" fill="#e5e7eb" font-size="10" font-weight="600" text-anchor="middle">{{ deltaHover.freqLabel }}</text>
                <text :x="deltaHover.labelX" y="27" fill="#9ca3af" font-size="9" text-anchor="middle">{{ deltaHover.valueLabel }}</text>
              </g>
            </svg>
          </div>
          <p v-if="frozen && generatedPoints.length" class="mt-2 text-xs text-vybes-text-secondary">
            <span style="color: #e879f9">━</span> proposed EQ correction ·
            <span class="text-gray-300">┄</span> predicted result after EQ
          </p>
        </div>
        <p v-else class="mt-4 text-xs text-vybes-text-secondary">
          Start the microphone while music (ideally
          <router-link to="/tools" class="text-vybes-accent underline">pink noise</router-link>)
          is playing to see where the room and system deviate from the source.
        </p>
      </CardSection>

      <CardSection v-if="frozen && deltaValues" title="EQ Correction">
        <p class="text-xs text-vybes-text-secondary mb-4">
          Fits parametric EQ bands that pull the frozen deviation toward the target curve.
          Tune the parameters below — the chart above previews the correction (magenta) and
          the predicted result (dashed) live.
        </p>

        <div class="grid sm:grid-cols-2 gap-x-6 gap-y-4">
          <div>
            <RangeSlider
              label="Target tilt"
              :min="-2"
              :max="1"
              :step="0.1"
              unit="dB/oct"
              :decimals="1"
              v-model="eqGen.tilt"
            />
            <p class="mt-1 text-xs text-vybes-text-secondary">
              0 reproduces the source exactly; negative tilts the target down toward the
              treble (warmer). In-room responses corrected fully flat often sound bright —
              −0.5 to −1 is a common preference.
            </p>
          </div>
          <RangeSlider
            label="Correction strength"
            :min="0"
            :max="100"
            :step="5"
            unit="%"
            :decimals="0"
            v-model="eqGen.strength"
          />
          <RangeSlider
            label="Max boost"
            :min="0"
            :max="12"
            :step="0.5"
            unit="dB"
            :decimals="1"
            v-model="eqGen.maxBoost"
          />
          <RangeSlider
            label="Max cut"
            :min="0"
            :max="15"
            :step="0.5"
            unit="dB"
            :decimals="1"
            v-model="eqGen.maxCut"
          />
          <RangeSlider
            label="Low frequency limit"
            :min="20"
            :max="500"
            :step="1"
            unit="Hz"
            :decimals="0"
            :logarithmic="true"
            v-model="eqGen.loHz"
          />
          <RangeSlider
            label="High frequency limit"
            :min="1000"
            :max="20000"
            :step="10"
            unit="Hz"
            :decimals="0"
            :logarithmic="true"
            v-model="eqGen.hiHz"
          />
          <RangeSlider
            label="Max bands"
            :min="1"
            :max="12"
            :step="1"
            unit=""
            :decimals="0"
            v-model="eqGen.maxBands"
          />
        </div>

        <div class="mt-5 pt-4 border-t border-vybes-dark-input">
          <template v-if="generatedPoints.length">
            <div class="flex flex-wrap gap-2 mb-4">
              <span
                v-for="(p, i) in generatedPoints"
                :key="'gen' + i"
                class="px-2.5 py-1 rounded-full text-xs font-mono bg-black/30 border border-vybes-dark-input text-vybes-text-primary"
              >
                {{ fmtHz(p.freq) }} · {{ p.gain > 0 ? '+' : '' }}{{ p.gain.toFixed(1) }} dB · Q {{ p.q.toFixed(2) }}
              </span>
            </div>
            <button
              class="btn-primary"
              :disabled="!activePresetName || applyState.busy"
              @click="showApplyModal = true"
            >
              Apply {{ generatedPoints.length }} band{{ generatedPoints.length === 1 ? '' : 's' }}
              to “{{ activePresetName || '…' }}”
            </button>
            <p v-if="!activePresetName" class="mt-2 text-xs text-vybes-text-secondary">
              Waiting for the active preset name from the device…
            </p>
          </template>
          <p v-else class="text-xs text-vybes-text-secondary">
            Nothing to correct — the deviation stays within ±1 dB of the target inside the
            current frequency limits.
          </p>
          <p v-if="applyState.message" class="mt-3 text-xs" :class="applyState.error ? 'text-red-400' : 'text-green-400'">
            {{ applyState.message }}
            <router-link
              v-if="!applyState.error && activePresetName"
              :to="`/preset/${encodeURIComponent(activePresetName)}`"
              class="text-vybes-accent underline ml-1"
            >
              Fine-tune in the preset editor
            </router-link>
          </p>
        </div>
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
            <p v-if="!frozen && micActive && sourceLive" class="mt-2 text-xs text-vybes-text-secondary">
              Freeze to convert the deviation into parametric EQ bands.
            </p>
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

    <ModalDialog
      v-model="showApplyModal"
      title="Apply EQ correction"
      confirm-text="Apply &amp; save"
      @confirm="applyGeneratedEq"
    >
      <p class="text-sm text-vybes-text-secondary">
        This replaces the preference EQ of preset
        <span class="font-semibold text-vybes-text-primary">“{{ activePresetName }}”</span>
        with the {{ generatedPoints.length }} generated band{{ generatedPoints.length === 1 ? '' : 's' }}
        and saves it to the device. Any EQ bands currently in that preset will be overwritten.
      </p>
    </ModalDialog>
  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted, onUnmounted } from 'vue';
import apiClient from '../api-client.js';
import CardSection from '../components/shared/CardSection.vue';
import SelectGroup from '../components/shared/SelectGroup.vue';
import RangeSlider from '../components/shared/RangeSlider.vue';
import ModalDialog from '../components/shared/ModalDialog.vue';
import { peqSumDb, fitPeqPoints } from '../eq-math.js';
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
  if (!data) return;
  if (data.messageType === 'activePresetChanged' && data.activePresetName) {
    activePresetName.value = data.activePresetName;
    return;
  }
  if (data.type !== 'rta' || typeof data.d !== 'string') return;
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

// Per-band deviation (mic − source, level-aligned), NaN where the source has
// effectively no content - the delta there is just noise-floor arithmetic,
// not room response.
const deltaValues = computed(() => {
  if (!micDb.value || !sourceDb.value || !sourceLive.value) return null;
  const out = new Array(RTA_NUM_BANDS);
  for (let i = 0; i < RTA_NUM_BANDS; i++) {
    out[i] = sourceDb.value[i] <= -85 || micDb.value[i] <= -110
      ? NaN
      : micDb.value[i] - offset.value - sourceDb.value[i];
  }
  return out;
});

const clampDelta = (d) => Math.max(-DELTA_RANGE_DB, Math.min(DELTA_RANGE_DB, d));

const deltaBars = computed(() => {
  if (!deltaValues.value) return [];
  const bars = [];
  const bw = ((width.value - padLeft) / RTA_NUM_BANDS) * 0.66;
  for (let i = 0; i < RTA_NUM_BANDS; i++) {
    if (!Number.isFinite(deltaValues.value[i])) continue;
    const d = clampDelta(deltaValues.value[i]);
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

// --- Delta chart crosshair ---
const fmtHz = (f) => (f >= 1000 ? `${f / 1000} kHz` : `${f} Hz`);
const hoverBandIndex = ref(null);

function onDeltaHover(event) {
  const rect = event.currentTarget.getBoundingClientRect();
  const bw = (width.value - padLeft) / RTA_NUM_BANDS;
  const i = Math.round((event.clientX - rect.left - padLeft) / bw - 0.5);
  hoverBandIndex.value = Math.min(RTA_NUM_BANDS - 1, Math.max(0, i));
}

function onDeltaLeave(event) {
  // A finger lifting fires pointerleave too; keep the touch crosshair
  // sticky so the reading survives to be acted on.
  if (event.pointerType === 'mouse') hoverBandIndex.value = null;
}

const deltaHover = computed(() => {
  if (hoverBandIndex.value === null) return null;
  const i = hoverBandIndex.value;
  const x = bandX(i);
  const d = deltaValues.value ? deltaValues.value[i] : NaN;
  const hasValue = Number.isFinite(d);
  return {
    x,
    y: hasValue ? deltaDbToY(clampDelta(d)) : deltaZeroY,
    hasValue,
    labelX: Math.min(width.value - 50, Math.max(padLeft + 50, x)),
    freqLabel: fmtHz(RTA_BAND_CENTERS[i]),
    valueLabel: hasValue ? `${d >= 0 ? '+' : ''}${d.toFixed(1)} dB` : 'no signal',
  };
});

// --- Diff → parametric EQ conversion (available while frozen) ---
const eqGen = reactive({
  tilt: -0.5, // dB/octave, pivoted at 1kHz
  strength: 100, // % of the deviation to correct
  maxBoost: 6, // dB - boosting room nulls wastes headroom, so capped low
  maxCut: 12, // dB
  loHz: 25,
  hiHz: 10000, // above ~10kHz a single mic position isn't trustworthy
  maxBands: 8,
});

const activePresetName = ref('');
const showApplyModal = ref(false);
const applyState = reactive({ busy: false, message: '', error: false });

// Desired EQ gain per band: negate the deviation from the tilted target,
// scale by strength, clamp to the boost/cut limits. NaN = leave alone.
const correctionTarget = computed(() => {
  if (!frozen.value || !deltaValues.value) return null;
  return RTA_BAND_CENTERS.map((fc, i) => {
    const d = deltaValues.value[i];
    if (!Number.isFinite(d) || fc < eqGen.loHz || fc > eqGen.hiHz) return NaN;
    const target = eqGen.tilt * Math.log2(fc / 1000);
    const c = -(d - target) * (eqGen.strength / 100);
    return Math.min(eqGen.maxBoost, Math.max(-eqGen.maxCut, c));
  });
});

const generatedPoints = computed(() => {
  if (!correctionTarget.value) return [];
  return fitPeqPoints(RTA_BAND_CENTERS, correctionTarget.value, {
    maxBands: Math.round(eqGen.maxBands),
  });
});

// The delta chart's x axis is linear in band index, and the band centers
// are log-spaced (fc ≈ 10^(1.3 + i/10)), so x maps back to frequency.
const xToBandFreq = (x) => {
  const bw = (width.value - padLeft) / RTA_NUM_BANDS;
  return Math.pow(10, 1.3 + ((x - padLeft) / bw - 0.5) / 10);
};

const correctionPath = computed(() => {
  if (!generatedPoints.value.length) return '';
  const seg = [];
  for (let x = padLeft; x <= width.value; x += 4) {
    const db = clampDelta(peqSumDb(generatedPoints.value, xToBandFreq(x)));
    seg.push(`${x.toFixed(1)},${deltaDbToY(db).toFixed(1)}`);
  }
  return `M ${seg.join(' L ')}`;
});

const predictedPath = computed(() => {
  if (!generatedPoints.value.length || !deltaValues.value) return '';
  const seg = [];
  for (let i = 0; i < RTA_NUM_BANDS; i++) {
    const d = deltaValues.value[i];
    if (!Number.isFinite(d)) continue;
    const db = clampDelta(d + peqSumDb(generatedPoints.value, RTA_BAND_CENTERS[i]));
    seg.push(`${bandX(i).toFixed(1)},${deltaDbToY(db).toFixed(1)}`);
  }
  return seg.length > 1 ? `M ${seg.join(' L ')}` : '';
});

async function applyGeneratedEq() {
  showApplyModal.value = false;
  if (!activePresetName.value || !generatedPoints.value.length) return;
  applyState.busy = true;
  applyState.message = '';
  const points = generatedPoints.value.map((p, id) => ({ id, freq: p.freq, gain: p.gain, q: p.q }));
  try {
    await apiClient.savePrefEqSet(activePresetName.value, points);
    applyState.error = false;
    applyState.message = `Saved ${points.length} band${points.length === 1 ? '' : 's'} to “${activePresetName.value}”.`;
  } catch (err) {
    applyState.error = true;
    applyState.message = `Failed to apply EQ: ${err.message}`;
  } finally {
    applyState.busy = false;
  }
}

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

  // Needed to know where "Apply EQ" saves; refreshed by activePresetChanged
  apiClient
    .getPresets()
    .then((presets) => {
      const current = presets.find((p) => p.isCurrent);
      if (current) activePresetName.value = current.name;
    })
    .catch(() => { /* device offline - the apply button stays disabled */ });

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
