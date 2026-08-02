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
            <span class="inline-block w-3 h-0.5 rounded bg-vybes-primary"></span>
            <span class="text-vybes-text-secondary">Source</span>
            <span :class="sourceLive ? 'text-vybes-live' : 'text-vybes-text-secondary'">
              {{ sourceLive ? 'live' : 'waiting for device…' }}
            </span>
          </span>
          <span class="flex items-center gap-1.5">
            <span class="inline-block w-3 h-0.5 rounded bg-vybes-live"></span>
            <span class="text-vybes-text-secondary">Microphone</span>
            <span :class="micActive ? 'text-vybes-live' : 'text-vybes-text-secondary'">
              {{ micActive ? (calPoints ? 'on (calibrated)' : 'on') : 'off' }}
            </span>
          </span>
          <span v-if="micActive && sourceLive" class="text-vybes-text-secondary tabular-nums">
            level aligned {{ offset >= 0 ? '+' : '' }}{{ offset.toFixed(1) }} dB
          </span>
        </div>

        <!-- Overlay chart -->
        <div ref="chartContainer" class="w-full rounded bg-black/30 overflow-hidden">
          <svg
            :width="width"
            :height="chartHeight"
            class="block cursor-crosshair"
            style="touch-action: pan-y"
            @pointermove="onSpectrumHover"
            @pointerdown="onSpectrumHover"
            @pointerleave="onSpectrumLeave"
            @touchstart="onChartTouchStart"
            @touchmove="onChartTouchMove"
          >
            <!-- dB gridlines -->
            <g v-for="line in dbGridLines" :key="'db' + line.db">
              <line class="grid-line" :x1="padLeft" :y1="line.y" :x2="width" :y2="line.y" />
              <text class="grid-label" :x="4" :y="line.y + 3" font-size="9">{{ line.db }}</text>
            </g>
            <!-- frequency gridlines -->
            <g v-for="line in freqGridLines" :key="'f' + line.label">
              <line class="grid-line" :x1="line.x" :y1="0" :x2="line.x" :y2="chartHeight - 14" />
              <text class="grid-label" :x="line.x" :y="chartHeight - 4" font-size="9" text-anchor="middle">{{ line.label }}</text>
            </g>
            <path v-if="sourcePath" class="trace-source" :d="sourcePath" fill="none" stroke-width="2" opacity="0.9" />
            <path v-if="micPath" class="trace-mic" :d="micPath" fill="none" stroke-width="2" opacity="0.9" />
            <!-- Crosshair: nearest band under the pointer/finger -->
            <g v-if="spectrumHover" pointer-events="none">
              <line :x1="spectrumHover.x" :y1="0" :x2="spectrumHover.x" :y2="chartHeight - 14" stroke="#fff" stroke-width="1" opacity="0.5" />
              <circle v-if="spectrumHover.sourceY !== null" :cx="spectrumHover.x" :cy="spectrumHover.sourceY" r="3" class="dot-source" />
              <circle v-if="spectrumHover.micY !== null" :cx="spectrumHover.x" :cy="spectrumHover.micY" r="3" class="dot-mic" />
              <rect class="hover-label-box" :x="spectrumHover.labelX - spectrumHover.boxW / 2" y="4" :width="spectrumHover.boxW" height="28" rx="4" />
              <text class="hover-label-freq" :x="spectrumHover.labelX" y="16" font-size="10" font-weight="600" text-anchor="middle">{{ spectrumHover.freqLabel }}</text>
              <text class="hover-label-value" :x="spectrumHover.labelX" y="27" font-size="9" text-anchor="middle">{{ spectrumHover.valueLabel }}</text>
            </g>
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
              @touchstart="onChartTouchStart"
              @touchmove="onChartTouchMove"
            >
              <g v-for="line in deltaGridLines" :key="'d' + line.db">
                <line class="grid-line" :x1="padLeft" :y1="line.y" :x2="width" :y2="line.y" />
                <text class="grid-label" :x="4" :y="line.y + 3" font-size="9">{{ line.db > 0 ? '+' + line.db : line.db }}</text>
              </g>
              <line class="grid-line-strong" :x1="padLeft" :y1="deltaZeroY" :x2="width" :y2="deltaZeroY" stroke-width="1.5" />
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
                <path class="trace-correction" :d="correctionPath" fill="none" stroke-width="2" opacity="0.9" />
                <path class="trace-predicted" :d="predictedPath" fill="none" stroke-width="1.5" stroke-dasharray="4 3" opacity="0.75" />
              </template>
              <!-- Crosshair: nearest band under the pointer/finger -->
              <g v-if="deltaHover" pointer-events="none">
                <line :x1="deltaHover.x" :y1="0" :x2="deltaHover.x" :y2="deltaHeight" stroke="#fff" stroke-width="1" opacity="0.5" />
                <circle v-if="deltaHover.hasValue" :cx="deltaHover.x" :cy="deltaHover.y" r="3" fill="#fff" />
                <rect class="hover-label-box" :x="deltaHover.labelX - 46" y="4" width="92" height="28" rx="4" />
                <text class="hover-label-freq" :x="deltaHover.labelX" y="16" font-size="10" font-weight="600" text-anchor="middle">{{ deltaHover.freqLabel }}</text>
                <text class="hover-label-value" :x="deltaHover.labelX" y="27" font-size="9" text-anchor="middle">{{ deltaHover.valueLabel }}</text>
              </g>
            </svg>
          </div>
          <p v-if="frozen && generatedPoints.length" class="mt-2 text-xs text-vybes-text-secondary">
            <span class="legend-correction">━</span> proposed EQ correction ·
            <span class="text-vybes-text-primary">┄</span> predicted result after EQ
          </p>
        </div>
        <p v-else class="mt-4 text-xs text-vybes-text-secondary">
          Start the microphone while music (ideally
          <button class="text-vybes-accent underline cursor-pointer" @click="openNoiseGenerator">pink noise</button>)
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

        <div class="mt-5 pt-4 border-t border-vybes-border">
          <template v-if="generatedPoints.length">
            <div class="flex flex-wrap gap-2 mb-4">
              <span
                v-for="(p, i) in generatedPoints"
                :key="'gen' + i"
                class="px-2.5 py-1 rounded-full text-xs font-mono tabular-nums bg-black/30 border border-vybes-border text-vybes-text-primary"
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
          <p v-if="applyState.message" class="mt-3 text-xs" :class="applyState.error ? 'text-red-400' : 'text-vybes-live'">
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

            <div class="mt-4">
              <SelectGroup v-model="resolution" label="Resolution">
                <option :value="3">1/3 octave (31 bands)</option>
                <option :value="6">1/6 octave (61 bands)</option>
                <option :value="12">1/12 octave (121 bands)</option>
              </SelectGroup>
              <p class="mt-1 text-xs text-vybes-text-secondary">
                <template v-if="sourceLive && sourceNativeBpo < Number(resolution)">
                  The device streams 1/{{ sourceNativeBpo }}-octave bands, so the source trace,
                  deviation and EQ math stay at that resolution; the microphone trace uses the
                  full selection.
                </template>
                <template v-else>
                  Finer resolutions pinpoint peaks and dips more precisely but look busier and
                  average more slowly.
                </template>
              </p>
            </div>
          </div>
        </div>

        <div class="mt-6 pt-4 border-t border-vybes-border">
          <p class="text-sm font-medium mb-2">Microphone calibration</p>
          <div class="flex flex-wrap items-center gap-3">
            <label class="btn-secondary cursor-pointer">
              Import cal file
              <input type="file" accept=".txt,.cal,.frd,.csv" class="hidden" @change="onCalFileSelected" />
            </label>
            <template v-if="calPoints">
              <span class="text-xs text-vybes-live">{{ calName }}</span>
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
import { ref, reactive, computed, watch, onMounted, onUnmounted } from 'vue';
import apiClient from '../api-client.js';
import { useGeneratorStore } from '../stores/generator.js';
import CardSection from '../components/shared/CardSection.vue';
import SelectGroup from '../components/shared/SelectGroup.vue';
import RangeSlider from '../components/shared/RangeSlider.vue';
import ModalDialog from '../components/shared/ModalDialog.vue';
import { peqSumDb, fitPeqPoints } from '../eq-math.js';
import {
  makeBandGrid,
  decodeRtaFrame,
  bandsFromFFT,
  aggregateBands,
  parseCalibrationFile,
  calCurveForGrid,
  medianOffset,
} from '../rta.js';

const CAL_STORAGE_KEY = 'vybes-rta-mic-cal';
const RESOLUTION_STORAGE_KEY = 'vybes-rta-resolution';
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

// --- Band grids ---
// The display resolution is a user setting; the mic trace always renders at
// it (the browser FFT has ~6Hz bins, plenty for 1/12 octave). The device
// streams at its own native resolution, inferred from frame length - the
// source trace, deviation and EQ math run on the coarser of the two grids.
const storedResolution = Number(localStorage.getItem(RESOLUTION_STORAGE_KEY));
const resolution = ref([3, 6, 12].includes(storedResolution) ? storedResolution : 3);
const sourceNativeBpo = ref(3); // bands/octave of the last device frame

const displayGrid = computed(() => makeBandGrid(Number(resolution.value)));
// Common grid for source trace, delta and EQ: device native, capped at the
// display selection.
const compareGrid = computed(() =>
  makeBandGrid(Math.min(sourceNativeBpo.value, Number(resolution.value)))
);

// --- Live state ---
const sourceDb = ref(null); // Float32Array on the device's native grid, or null
const micDb = ref(null); // Float32Array on displayGrid, or null
const nowTick = ref(Date.now()); // refreshed by the poll timer for staleness checks
const lastSourceFrameAt = ref(0);
const micActive = ref(false);
const micError = ref('');
const frozen = ref(false);
const averagingSeconds = ref(2);
const offset = ref(0);

const calPoints = ref(null); // [[freq, gain], ...] from the cal file
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

// "pink noise" in the helper text opens the generator dock ready to start
const generator = useGeneratorStore();
function openNoiseGenerator() {
  generator.setSource('noise');
  generator.expanded = true;
}
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
  for (let i = 0; i < newDb.length; i++) {
    const p = Math.pow(10, newDb[i] / 10);
    avgPower[i] = avgPower[i] <= 0 ? p : avgPower[i] + alpha * (p - avgPower[i]);
  }
}

function powerToDb(avgPower) {
  const out = new Float32Array(avgPower.length);
  for (let i = 0; i < avgPower.length; i++) {
    out[i] = avgPower[i] > 1e-12 ? 10 * Math.log10(avgPower[i]) : -120;
  }
  return out;
}

// --- Source (device) spectrum over the live-updates websocket ---
let unsubscribeLive = null;
let keepaliveTimer = null;
let sourceAvgPower = new Float32Array(makeBandGrid(3).centers.length);
let lastSourceEmaAt = 0;

function onLiveMessage(data) {
  if (!data) return;
  if (data.messageType === 'activePresetChanged' && data.activePresetName) {
    activePresetName.value = data.activePresetName;
    return;
  }
  if (data.type !== 'rta' || typeof data.d !== 'string') return;
  const decoded = decodeRtaFrame(data.d);
  if (!decoded) return;
  const now = Date.now();
  lastSourceFrameAt.value = now;
  // Firmware resolution changed (or first frame after an upgrade): restart
  // the averaging on the new grid.
  if (decoded.grid.bandsPerOctave !== sourceNativeBpo.value) {
    sourceNativeBpo.value = decoded.grid.bandsPerOctave;
    sourceAvgPower = new Float32Array(decoded.grid.centers.length);
    lastSourceEmaAt = 0;
    sourceDb.value = null;
  }
  if (frozen.value) return;
  emaUpdate(sourceAvgPower, decoded.values, lastSourceEmaAt ? now - lastSourceEmaAt : 1000);
  lastSourceEmaAt = now;
  sourceDb.value = powerToDb(sourceAvgPower);
}

// Source trace on the common grid (aggregated down when the display
// selection is coarser than the device's native resolution).
const sourceCompareDb = computed(() =>
  sourceDb.value
    ? aggregateBands(sourceDb.value, makeBandGrid(sourceNativeBpo.value), compareGrid.value)
    : null
);

// Mic re-binned onto the common grid for delta/offset math.
const micCompareDb = computed(() =>
  micDb.value ? aggregateBands(micDb.value, displayGrid.value, compareGrid.value) : null
);

// --- Microphone spectrum via Web Audio ---
let micStream = null;
let audioContext = null;
let analyser = null;
let micPollTimer = null;
let micFreqData = null;
let micAvgPower = new Float32Array(displayGrid.value.centers.length);
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
  micAvgPower = new Float32Array(displayGrid.value.centers.length);
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

// Mic calibration interpolated onto the display grid.
const calGridCurve = computed(() =>
  calPoints.value ? calCurveForGrid(calPoints.value, displayGrid.value) : null
);

// Poll timer: sample the mic FFT, refresh staleness, recompute alignment
function pollTick() {
  nowTick.value = Date.now();
  if (!analyser || frozen.value) return;

  analyser.getFloatFrequencyData(micFreqData);
  const binWidth = audioContext.sampleRate / analyser.fftSize;
  const bands = bandsFromFFT(micFreqData, binWidth, displayGrid.value);
  if (calGridCurve.value) {
    for (let i = 0; i < bands.length; i++) bands[i] -= calGridCurve.value[i];
  }
  const now = Date.now();
  emaUpdate(micAvgPower, bands, lastMicEmaAt ? now - lastMicEmaAt : 1000);
  lastMicEmaAt = now;
  micDb.value = powerToDb(micAvgPower);

  // Level alignment: mic and source have unrelated absolute scales, so
  // shift the mic trace by the median mid-band difference before comparing.
  if (sourceCompareDb.value && sourceLive.value && micCompareDb.value) {
    offset.value = medianOffset(
      micCompareDb.value,
      sourceCompareDb.value,
      compareGrid.value.centers
    );
  }
}

// Changing resolution reshapes the mic averaging buffer, so restart it (the
// trace re-converges within the averaging window). Unfreeze - a frozen
// deviation from another grid no longer matches the new one.
watch(resolution, (v) => {
  localStorage.setItem(RESOLUTION_STORAGE_KEY, String(Number(v)));
  frozen.value = false;
  micAvgPower = new Float32Array(displayGrid.value.centers.length);
  lastMicEmaAt = 0;
  micDb.value = null;
});

// --- Calibration file handling ---
function onCalFileSelected(event) {
  calError.value = '';
  const file = event.target.files?.[0];
  event.target.value = '';
  if (!file) return;
  const reader = new FileReader();
  reader.onload = () => {
    const points = parseCalibrationFile(String(reader.result));
    if (!points) {
      calError.value = 'No “frequency gain” pairs found in that file.';
      return;
    }
    calPoints.value = points;
    calName.value = file.name;
    try {
      localStorage.setItem(CAL_STORAGE_KEY, JSON.stringify({ name: file.name, points }));
    } catch (e) { /* storage full or blocked - cal still applies this session */ }
  };
  reader.readAsText(file);
}

function clearCal() {
  calPoints.value = null;
  calName.value = '';
  localStorage.removeItem(CAL_STORAGE_KEY);
}

// --- Chart geometry ---
// Log-frequency x axis with a fixed span (the outer edges of the 1/3-octave
// 20Hz-20kHz range), so traces of different resolutions share the chart and
// the axis doesn't shift when the resolution changes.
const LOG_X_LO = 1.25; // log10(20) - half a 1/3-octave band
const LOG_X_HI = 4.35; // log10(20000) + half a 1/3-octave band

const xForFreq = (f) =>
  padLeft + ((Math.log10(f) - LOG_X_LO) / (LOG_X_HI - LOG_X_LO)) * (width.value - padLeft);
const freqAtX = (x) =>
  Math.pow(10, LOG_X_LO + ((x - padLeft) / (width.value - padLeft)) * (LOG_X_HI - LOG_X_LO));

// Pixel width of one band on a grid (bands are equal-width in log frequency)
const bandPixelWidth = (grid) =>
  (width.value - padLeft) / ((LOG_X_HI - LOG_X_LO) * grid.perDecade);

// Y scale of the overlay chart: 70dB window that tracks the loudest band.
const topDb = computed(() => {
  let max = -60;
  if (sourceCompareDb.value) for (const v of sourceCompareDb.value) max = Math.max(max, v);
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

const FREQ_MARKS = [
  [31.5, '31'], [63, '63'], [125, '125'], [250, '250'], [500, '500'],
  [1000, '1k'], [2000, '2k'], [4000, '4k'], [8000, '8k'], [16000, '16k'],
];

const freqGridLines = computed(() =>
  FREQ_MARKS.map(([f, label]) => ({ label, x: xForFreq(f) }))
);

function tracePath(values, grid, shift = 0) {
  const points = [];
  for (let i = 0; i < values.length; i++) {
    const db = values[i] - shift;
    if (db <= -110) continue; // don't draw the floor
    points.push(
      `${xForFreq(grid.centers[i]).toFixed(1)},${Math.min(chartHeight - 14, Math.max(0, dbToY(db))).toFixed(1)}`
    );
  }
  return points.length > 1 ? `M ${points.join(' L ')}` : '';
}

const sourcePath = computed(() =>
  sourceCompareDb.value ? tracePath(sourceCompareDb.value, compareGrid.value) : ''
);
// The mic trace is drawn pre-shifted onto the source's scale
const micPath = computed(() =>
  micDb.value ? tracePath(micDb.value, displayGrid.value, sourceLive.value ? offset.value : 0) : ''
);

// --- Delta chart ---
const deltaZeroY = deltaHeight / 2;
const deltaDbToY = (db) => deltaZeroY - (db / DELTA_RANGE_DB) * (deltaHeight / 2 - 8);

const deltaGridLines = computed(() =>
  [-20, -10, 10, 20].map((db) => ({ db, y: deltaDbToY(db) }))
);

// Per-band deviation (mic − source, level-aligned) on the common grid, NaN
// where the source has effectively no content - the delta there is just
// noise-floor arithmetic, not room response.
const deltaValues = computed(() => {
  if (!micCompareDb.value || !sourceCompareDb.value || !sourceLive.value) return null;
  const n = compareGrid.value.centers.length;
  const out = new Array(n);
  for (let i = 0; i < n; i++) {
    out[i] = sourceCompareDb.value[i] <= -85 || micCompareDb.value[i] <= -110
      ? NaN
      : micCompareDb.value[i] - offset.value - sourceCompareDb.value[i];
  }
  return out;
});

const clampDelta = (d) => Math.max(-DELTA_RANGE_DB, Math.min(DELTA_RANGE_DB, d));

// Bar fills are bound per-bar, so they can't come from a stylesheet rule.
// Kept in sync with the theme amber (--vybes-accent) by hand.
const DELTA_BOOST_COLOR = '#f5c04e';
const DELTA_LOSS_COLOR = '#38bdf8';

const deltaBars = computed(() => {
  if (!deltaValues.value) return [];
  const grid = compareGrid.value;
  const bars = [];
  const bw = bandPixelWidth(grid) * 0.66;
  for (let i = 0; i < grid.centers.length; i++) {
    if (!Number.isFinite(deltaValues.value[i])) continue;
    const d = clampDelta(deltaValues.value[i]);
    const y0 = deltaDbToY(Math.max(0, d));
    bars.push({
      index: i,
      x: xForFreq(grid.centers[i]) - bw / 2,
      y: y0,
      w: bw,
      h: Math.max(1, Math.abs(deltaDbToY(d) - deltaZeroY)),
      color: d >= 0 ? DELTA_BOOST_COLOR : DELTA_LOSS_COLOR,
      opacity: Math.abs(d) < 2 ? 0.35 : 0.9,
    });
  }
  return bars;
});

// --- Chart crosshairs ---

// touch-action: pan-y lets vertical swipes scroll the page, but the browser
// may also claim a mostly-horizontal crosshair drag the moment it drifts
// vertically - it fires pointercancel and the line freezes until a new
// touch. Decide the gesture's intent from its first few pixels instead:
// horizontal locks the whole gesture to the crosshair (preventDefault keeps
// the scroller from taking over), vertical hands it to the browser.
const DRAG_LOCK_THRESHOLD_PX = 6;
let touchStartX = 0;
let touchStartY = 0;
let touchLock = null; // null = undecided, then 'drag' or 'scroll'

function onChartTouchStart(event) {
  const t = event.touches[0];
  touchStartX = t.clientX;
  touchStartY = t.clientY;
  touchLock = null;
}

function onChartTouchMove(event) {
  if (touchLock === null) {
    const dx = Math.abs(event.touches[0].clientX - touchStartX);
    const dy = Math.abs(event.touches[0].clientY - touchStartY);
    if (Math.max(dx, dy) >= DRAG_LOCK_THRESHOLD_PX) {
      touchLock = dx > dy ? 'drag' : 'scroll';
    }
  }
  // Also prevented while undecided, so the page doesn't creep during the
  // first few pixels of what turns out to be a crosshair drag.
  if (touchLock !== 'scroll' && event.cancelable) event.preventDefault();
}

const fmtHz = (f) => {
  const r = Number(f.toPrecision(3));
  return r >= 1000 ? `${Number((r / 1000).toPrecision(3))} kHz` : `${r} Hz`;
};

// Nearest band of a grid for a pointer event on either chart
function pointerBandIndex(event, grid) {
  const rect = event.currentTarget.getBoundingClientRect();
  const f = freqAtX(event.clientX - rect.left);
  const k = Math.round(grid.perDecade * Math.log10(f)) - grid.kLo;
  return Math.min(grid.centers.length - 1, Math.max(0, k));
}

// Nearest index of `grid` for a frequency (used to read the source value
// under a display-grid crosshair when the grids differ).
function gridIndexForFreq(grid, freq) {
  const k = Math.round(grid.perDecade * Math.log10(freq)) - grid.kLo;
  return Math.min(grid.centers.length - 1, Math.max(0, k));
}

// A finger lifting fires pointerleave too; keep the touch crosshair
// sticky so the reading survives to be acted on.
const spectrumHoverIndex = ref(null);

function onSpectrumHover(event) {
  spectrumHoverIndex.value = pointerBandIndex(event, displayGrid.value);
}

function onSpectrumLeave(event) {
  if (event.pointerType === 'mouse') spectrumHoverIndex.value = null;
}

const spectrumHover = computed(() => {
  if (spectrumHoverIndex.value === null) return null;
  const grid = displayGrid.value;
  const i = Math.min(grid.centers.length - 1, spectrumHoverIndex.value);
  const freq = grid.centers[i];
  const x = xForFreq(freq);
  const clampY = (db) => Math.min(chartHeight - 14, Math.max(0, dbToY(db)));
  // Source is drawn on the common grid; read the band under the cursor
  const src = sourceCompareDb.value
    ? sourceCompareDb.value[gridIndexForFreq(compareGrid.value, freq)]
    : NaN;
  // Read the mic on the same shifted scale it's drawn at
  const mic = micDb.value ? micDb.value[i] - (sourceLive.value ? offset.value : 0) : NaN;
  const hasSrc = Number.isFinite(src) && src > -110;
  const hasMic = Number.isFinite(mic) && mic > -110;
  const parts = [];
  if (hasSrc) parts.push(`src ${src.toFixed(1)}`);
  if (hasMic) parts.push(`mic ${mic.toFixed(1)}`);
  const valueLabel = parts.length ? `${parts.join(' · ')} dB` : 'no signal';
  const boxW = Math.max(72, 14 + valueLabel.length * 4.8);
  return {
    x,
    sourceY: hasSrc ? clampY(src) : null,
    micY: hasMic ? clampY(mic) : null,
    labelX: Math.min(width.value - boxW / 2 - 4, Math.max(padLeft + boxW / 2 + 4, x)),
    boxW,
    freqLabel: fmtHz(freq),
    valueLabel,
  };
});

const hoverBandIndex = ref(null);

function onDeltaHover(event) {
  hoverBandIndex.value = pointerBandIndex(event, compareGrid.value);
}

function onDeltaLeave(event) {
  if (event.pointerType === 'mouse') hoverBandIndex.value = null;
}

const deltaHover = computed(() => {
  if (hoverBandIndex.value === null) return null;
  const grid = compareGrid.value;
  const i = Math.min(grid.centers.length - 1, hoverBandIndex.value);
  const x = xForFreq(grid.centers[i]);
  const d = deltaValues.value ? deltaValues.value[i] : NaN;
  const hasValue = Number.isFinite(d);
  return {
    x,
    y: hasValue ? deltaDbToY(clampDelta(d)) : deltaZeroY,
    hasValue,
    labelX: Math.min(width.value - 50, Math.max(padLeft + 50, x)),
    freqLabel: fmtHz(grid.centers[i]),
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
  return compareGrid.value.centers.map((fc, i) => {
    const d = deltaValues.value[i];
    if (!Number.isFinite(d) || fc < eqGen.loHz || fc > eqGen.hiHz) return NaN;
    const target = eqGen.tilt * Math.log2(fc / 1000);
    const c = -(d - target) * (eqGen.strength / 100);
    return Math.min(eqGen.maxBoost, Math.max(-eqGen.maxCut, c));
  });
});

const generatedPoints = computed(() => {
  if (!correctionTarget.value) return [];
  return fitPeqPoints(compareGrid.value.centers, correctionTarget.value, {
    maxBands: Math.round(eqGen.maxBands),
  });
});

const correctionPath = computed(() => {
  if (!generatedPoints.value.length) return '';
  const seg = [];
  for (let x = padLeft; x <= width.value; x += 4) {
    const db = clampDelta(peqSumDb(generatedPoints.value, freqAtX(x)));
    seg.push(`${x.toFixed(1)},${deltaDbToY(db).toFixed(1)}`);
  }
  return `M ${seg.join(' L ')}`;
});

const predictedPath = computed(() => {
  if (!generatedPoints.value.length || !deltaValues.value) return '';
  const grid = compareGrid.value;
  const seg = [];
  for (let i = 0; i < grid.centers.length; i++) {
    const d = deltaValues.value[i];
    if (!Number.isFinite(d)) continue;
    const db = clampDelta(d + peqSumDb(generatedPoints.value, grid.centers[i]));
    seg.push(`${xForFreq(grid.centers[i]).toFixed(1)},${deltaDbToY(db).toFixed(1)}`);
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
    if (stored?.points?.length >= 2) {
      calPoints.value = stored.points;
      calName.value = stored.name || 'stored calibration';
    } else if (stored?.curve?.length === 31) {
      // Pre-resolution storage format: per-band corrections on the
      // 1/3-octave grid. Reconstruct points at those centers.
      calPoints.value = makeBandGrid(3).centers.map((fc, i) => [fc, stored.curve[i]]);
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

<style scoped>
/* SVG paint can't use Tailwind colour utilities, so the chart's chrome
   reads the theme variables directly (same ramp as ParametricEQ). */
.grid-line {
  stroke: var(--vybes-grid-line);
  stroke-width: 1;
}

.grid-line-strong {
  stroke: var(--vybes-grid-line-strong);
}

.grid-label {
  fill: var(--vybes-text-secondary);
}

.trace-source {
  stroke: var(--vybes-primary);
}

.trace-mic {
  stroke: var(--vybes-live);
}

.trace-correction {
  stroke: #e879f9; /* magenta: proposed correction, distinct from both traces */
}

.trace-predicted {
  stroke: var(--vybes-text-primary);
}

.legend-correction {
  color: #e879f9;
}

.dot-source {
  fill: var(--vybes-primary);
}

.dot-mic {
  fill: var(--vybes-live);
}

.hover-label-box {
  fill: rgba(10, 13, 17, 0.9);
  stroke: var(--vybes-border);
}

.hover-label-freq {
  fill: var(--vybes-text-primary);
  font-variant-numeric: tabular-nums;
}

.hover-label-value {
  fill: var(--vybes-text-secondary);
  font-variant-numeric: tabular-nums;
}
</style>
