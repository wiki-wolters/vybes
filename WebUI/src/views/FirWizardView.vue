<template>
  <div class="container mx-auto px-3 sm:px-4 py-3 max-w-3xl min-h-[calc(100vh-200px)]">
    <div v-if="errorMessage" class="mb-4 p-4 rounded-md text-sm bg-red-700 text-red-100" @click="errorMessage = ''">
      {{ errorMessage }}
    </div>

    <div class="flex items-center justify-between mb-4">
      <h2 class="text-2xl font-semibold text-vybes-text-primary">Auto-FIR wizard</h2>
      <button class="btn-secondary" @click="quit">Close</button>
    </div>

    <ol class="flex flex-wrap gap-1 mb-6 text-xs text-vybes-text-secondary">
      <li v-for="(s, i) in STAGE_ORDER" :key="s"
          class="px-2 py-1 rounded-full"
          :class="s === stage ? 'bg-vybes-primary text-white' : (STAGE_ORDER.indexOf(stage) > i ? 'bg-vybes-dark-input' : '')">
        {{ i }}. {{ STAGE_LABELS[s] }}
      </li>
    </ol>

    <Loading v-if="!store.preset" message="Loading preset…" />

    <!-- ===================== 0: PLAN ===================== -->
    <CardSection v-else-if="stage === 'plan'" title="Plan the tap budget">
      <p class="text-sm text-vybes-text-secondary mb-4">
        Each corrected output's kernel needs enough taps to reach down to its own
        crossover's low edge. Nothing plays yet - this only checks the plan
        fits the device's shared FIR pool.
      </p>

      <div class="space-y-3 mb-4">
        <div v-for="row in planRows" :key="row.index" class="flex items-center gap-3">
          <div class="w-32 flex-none truncate text-sm">{{ row.label }}</div>
          <div class="w-28 flex-none text-xs text-vybes-text-secondary tabular-nums">
            {{ formatValue(row.fLo, '', 0) }}–{{ formatValue(row.fHi, '', 0) }} Hz
          </div>
          <div class="flex-1">
            <RangeSlider
              v-if="row.correctable"
              :model-value="row.taps"
              :min="0" :max="8192" :step="128" :decimals="0" unit=" taps"
              @update:modelValue="setPlanTaps(row.index, $event)"
            />
            <span v-else class="text-xs text-vybes-text-secondary">
              not corrected (below the FIR/EQ transition)
            </span>
          </div>
        </div>
      </div>

      <FirPoolBar :used="planTotal" :total="poolTotal" :errors="[]" />
      <p v-if="planTotal > poolTotal" class="mt-2 text-sm text-red-400">
        This plan exceeds the tap pool by {{ formatValue(planTotal - poolTotal, '', 0) }} taps - reduce an output above.
      </p>

      <div class="flex justify-end mt-6">
        <button class="btn-primary" :disabled="planTotal > poolTotal || correctedOutputs.length === 0" @click="confirmPlan">
          Continue
        </button>
      </div>
    </CardSection>

    <!-- ===================== 1: SETUP ===================== -->
    <CardSection v-else-if="stage === 'setup'" title="Set up the microphone">
      <ul class="text-sm text-vybes-text-secondary list-disc pl-5 space-y-1 mb-4">
        <li>Put the phone at your listening seat and keep it still for the whole measurement.</li>
        <li>On iOS, set Control Center → Mic Mode → Wide Spectrum before starting (Voice Isolation reshapes the band we're measuring).</li>
        <li>This preset's FIR and EQ are turned off for the session (both restore automatically afterward) - crossovers and output PEQ stay active.</li>
        <li>Keep the room quiet; sweeps play at a fixed level, independent of the volume setting.</li>
      </ul>

      <p v-if="!micSupported" class="text-sm text-red-400 mb-3">
        Microphone capture isn't available here. Open the UI over HTTPS (required for mic access) and try again.
      </p>

      <div v-if="!recording">
        <button class="btn-primary" :disabled="!micSupported" @click="startCapture">Start capture</button>
      </div>
      <div v-else>
        <div v-if="captureChain" class="text-xs mb-3">
          <p class="text-vybes-text-secondary">
            Capture chain —
            <template v-for="(proc, i) in captureChain.processors" :key="proc.key">
              <span v-if="i">, </span>{{ proc.label }}:
              <span :class="{ 'text-red-400': proc.state === 'on', 'text-vybes-live': proc.state === 'off' }">
                {{ proc.state === 'unknown' ? 'not reported' : proc.state }}
              </span>
            </template>
            <template v-if="captureChain.sampleRate || captureChain.channels">
              · {{ [captureChain.sampleRate, captureChain.channels].filter(Boolean).join(' ') }}
            </template>
          </p>
          <p v-if="captureChain.processed" class="mt-1 text-red-400">
            The browser left processing on after being asked for a raw capture - the
            measurement will absorb it. On iOS check Mic Mode is Wide Spectrum, not Standard.
          </p>
          <p v-else-if="captureChain.unreported" class="mt-1 text-vybes-text-secondary">
            This browser reports nothing either way (Safari commonly omits these keys) - unconfirmed, not necessarily processed.
          </p>
        </div>
        <p class="text-sm text-vybes-live mb-3">Microphone is live and recording…</p>
        <div class="flex justify-end space-x-3">
          <button class="btn-secondary" @click="quit">Cancel</button>
          <button class="btn-primary" @click="beginMeasurement">Continue to measurement</button>
        </div>
      </div>
    </CardSection>

    <!-- ===================== 2: MEASURE ===================== -->
    <CardSection v-else-if="stage === 'measure'" title="Measuring">
      <p class="text-sm text-vybes-text-secondary mb-3">{{ measureLabel }}</p>
      <div class="h-2 rounded bg-vybes-dark-input overflow-hidden mb-4">
        <div class="h-full bg-vybes-primary transition-all duration-500" :style="{ width: `${measureProgressPct}%` }"></div>
      </div>
      <div class="flex justify-end">
        <button class="btn-secondary" @click="cancelMeasurement">Cancel</button>
      </div>
    </CardSection>

    <div v-else-if="stage === 'processing'" class="text-center text-sm text-vybes-text-secondary py-12">
      Crunching the capture…
    </div>

    <!-- ===================== 3: REVIEW ===================== -->
    <CardSection v-else-if="stage === 'review'" title="Review the measurement">
      <p class="text-sm text-vybes-text-secondary mb-2">
        Gated magnitude and excess phase, one output at a time. Reflections after the gate
        window don't show up here by design (quasi-anechoic time-gating).
      </p>
      <p class="text-sm text-vybes-text-secondary mb-2">
        Distortion comes free with a swept measurement: each harmonic order lands ahead of
        the impulse in the deconvolution, so the charts below are the same capture, read
        earlier. They include the phone microphone's own distortion, and anything sitting
        on the dashed floor is below what this measurement can see.
      </p>
      <p class="text-xs text-vybes-text-secondary mb-4">
        Drift: {{ measureResult.driftPpm.toFixed(1) }} ppm (confidence {{ (measureResult.driftConfidence * 100).toFixed(0) }}%)
      </p>

      <div v-for="row in reviewRows" :key="row.output" class="mb-6 pb-6 border-b border-vybes-border last:border-0">
        <div class="flex items-center justify-between mb-2">
          <h4 class="font-medium">{{ row.label }}</h4>
          <span class="text-xs text-vybes-text-secondary tabular-nums">SNR {{ row.snrDb.toFixed(0) }} dB</span>
        </div>
        <p v-if="row.lowSnr" class="text-xs text-yellow-400 mb-2">
          Low signal-to-noise - check the output is routed and audible from the seat.
        </p>
        <p v-if="row.contradiction" class="text-xs text-yellow-400 mb-2">
          The measured response doesn't roll off where its
          {{ row.contradiction.includes('hp') ? 'high-pass' : 'low-pass' }} is configured -
          check this output's crossover and routing.
        </p>
        <FirResponseChart :measurement="row.measurement" :band="row.band" />
        <div v-if="row.harmonics" class="mt-4">
          <FirDistortionChart :harmonics="row.harmonics" :band="row.band" />
        </div>
      </div>

      <div class="flex justify-end mt-4">
        <button class="btn-primary" @click="stage = 'design'">Continue to design</button>
      </div>
    </CardSection>

    <!-- ===================== 4: DESIGN ===================== -->
    <CardSection v-else-if="stage === 'design'" title="Design the correction">
      <div class="flex flex-wrap gap-2 mb-4">
        <button
          v-for="preset in RENDER_PRESETS" :key="preset.id"
          class="chip" :class="{ 'chip-active': renderChoice === preset.id }"
          @click="renderChoice = preset.id"
        >{{ preset.label }} ({{ preset.latencyBudgetMs }} ms)</button>
        <button class="chip" :class="{ 'chip-active': renderChoice === 'custom' }" @click="renderChoice = 'custom'">Custom</button>
      </div>
      <div v-if="renderChoice === 'custom'" class="mb-4">
        <RangeSlider v-model="customBudgetMs" :min="0" :max="30" :step="1" unit=" ms" label="Latency budget" />
      </div>
      <p class="text-xs text-vybes-text-secondary mb-4">
        <template v-if="reachHzValue">
          Phase correction reaches down to about {{ formatValue(reachHzValue, '', 0) }} Hz at this budget.
        </template>
        <template v-else>
          Minimum-phase render: magnitude-only correction, no added latency beyond one processing block.
        </template>
      </p>

      <div class="mb-4 max-w-sm">
        <SelectGroup v-model="target.mode" label="Target curve">
          <option value="tilt">Downward tilt</option>
          <option value="flat">Flat</option>
          <option v-for="c in TARGET_CURVE_PRESETS" :key="c.id" :value="c.id">{{ c.label }}</option>
          <option value="custom">Custom (imported)</option>
        </SelectGroup>
        <RangeSlider
          v-if="target.mode === 'tilt'"
          class="mt-3"
          label="Tilt"
          :min="-2"
          :max="1"
          :step="0.1"
          unit="dB/oct"
          :decimals="1"
          v-model="target.tiltDbPerOct"
        />
        <div v-if="target.mode === 'custom'" class="mt-3 flex flex-wrap items-center gap-3">
          <label class="btn-secondary cursor-pointer">
            Import target file
            <input type="file" accept=".txt,.cal,.frd,.csv" class="hidden" @change="onTargetFileSelected" />
          </label>
          <span v-if="target.customName" class="text-xs text-vybes-live">{{ target.customName }}</span>
        </div>
        <p v-if="targetError" class="mt-2 text-xs text-red-400">{{ targetError }}</p>
        <p class="mt-1 text-xs text-vybes-text-secondary">{{ targetHelp }}</p>
      </div>

      <button class="btn-secondary mb-4" @click="runDesign">{{ kernelsReady ? 'Re-design' : 'Design filters' }}</button>

      <div v-if="kernelsReady">
        <div v-for="row in designRows" :key="row.output" class="mb-6 pb-6 border-b border-vybes-border last:border-0">
          <h4 class="font-medium mb-2">{{ row.label }} — predicted result</h4>
          <FirResponseChart
            :measurement="row.measurement"
            :compare-measurement="predicted.get(row.output)"
            :target-db="targetCurves.get(row.output)"
            :band="row.band"
            show-legend
          />
        </div>
        <div class="flex justify-end">
          <button class="btn-primary" @click="stage = 'apply'">Continue to apply</button>
        </div>
      </div>
    </CardSection>

    <!-- ===================== 5: APPLY ===================== -->
    <CardSection v-else-if="stage === 'apply'" title="Apply">
      <p class="text-sm text-vybes-text-secondary mb-4">
        Writes the kernels to a copy of "{{ store.presetName }}" - the original preset is left
        untouched as your undo/A-B.
      </p>
      <InputGroup
        :model-value="destPresetName"
        @update:modelValue="setDestPresetName"
        label="New preset name"
        class="mb-1 max-w-xs"
      />
      <p class="text-xs text-vybes-text-secondary mb-4">{{ destPresetName.length }} / 15</p>

      <div v-if="applyError" class="text-sm text-red-400 mb-3">{{ applyError }}</div>

      <div class="flex justify-end space-x-3">
        <button class="btn-secondary" @click="stage = 'design'" :disabled="applying">Back</button>
        <button class="btn-primary" :disabled="applying || !destPresetName.trim()" @click="apply">
          {{ applying ? 'Applying…' : 'Apply' }}
        </button>
      </div>
    </CardSection>

    <!-- ===================== 6: VERIFY / DONE ===================== -->
    <CardSection v-else-if="stage === 'verify'" title="Done">
      <p class="text-sm text-vybes-live mb-4">
        "{{ appliedPresetName }}" now has the designed FIR kernels assigned and enabled.
      </p>
      <div class="flex flex-wrap gap-3">
        <button class="btn-secondary" @click="router.push(`/preset/${encodeURIComponent(appliedPresetName)}`)">
          View preset
        </button>
        <button class="btn-secondary" :disabled="verifying" @click="runVerify">
          {{ verifying ? 'Measuring…' : 'Activate & re-measure to verify' }}
        </button>
      </div>

      <div v-if="verifyResult" class="mt-6">
        <div v-for="row in verifyRows" :key="row.output" class="mb-6 pb-6 border-b border-vybes-border last:border-0">
          <h4 class="font-medium mb-2">{{ row.label }} — before / after</h4>
          <FirResponseChart :measurement="row.before" :compare-measurement="row.after" :band="row.band" show-legend />
        </div>
      </div>
    </CardSection>
  </div>
</template>

<script setup>
import { ref, reactive, computed, watch, onMounted, onUnmounted } from 'vue';
import { useRouter } from 'vue-router';
import apiClient from '../api-client.js';
import { usePresetStore } from '../stores/preset.js';
import { useGeneratorStore } from '../stores/generator.js';
import { MicRecorder, micSupported } from '../audio-capture.js';
import { describeCaptureSettings, parseCalibrationFile } from '../rta.js';
import { formatValue } from '../utilities.js';
import {
  TARGET_CURVE_PRESETS,
  DEFAULT_TARGET,
  targetModeHelp,
  readStoredTarget,
  writeStoredTarget,
} from '../target-curves.js';
import CardSection from '../components/shared/CardSection.vue';
import RangeSlider from '../components/shared/RangeSlider.vue';
import SelectGroup from '../components/shared/SelectGroup.vue';
import InputGroup from '../components/shared/InputGroup.vue';
import FirPoolBar from '../components/shared/FirPoolBar.vue';
import Loading from '../components/shared/Loading.vue';
import FirResponseChart from '../components/fir-wizard/FirResponseChart.vue';
import FirDistortionChart from '../components/fir-wizard/FirDistortionChart.vue';
import {
  DEVICE_SAMPLE_RATE,
  planWizardTapBudget,
  analysisBandFor,
  parseSweepEventLine,
  sweepDurationS,
  processSweepCapture,
  computeRelativeDelays,
  passbandContradiction,
  LOW_SNR_DB,
  RENDER_PRESETS,
  reachHz,
  designOutputKernel,
  targetDbForOutput,
  buildApplyPlan,
  MAX_DELAY_US,
} from '../fir-wizard.js';
import { predictCorrected } from '../fir-design.js';

const router = useRouter();
const store = usePresetStore();
const gen = useGeneratorStore();

const SWEEP_LEVEL = 50;

const STAGE_ORDER = ['plan', 'setup', 'measure', 'review', 'design', 'apply', 'verify'];
const STAGE_LABELS = {
  plan: 'Plan', setup: 'Setup', measure: 'Measure', processing: 'Measure',
  review: 'Review', design: 'Design', apply: 'Apply', verify: 'Verify',
};
const stage = ref('plan');
const errorMessage = ref('');

function fail(message) {
  errorMessage.value = message;
}

// ===== Stage 0: Plan =====

const poolTotal = ref(12288);
const planRows = ref([]);

async function loadPlan() {
  try {
    const pool = await apiClient.getFirPool(store.presetName);
    poolTotal.value = pool.total;
  } catch (e) {
    console.error('Failed to load FIR pool', e);
  }
  const plan = planWizardTapBudget(store.preset, { poolTotal: poolTotal.value });
  planRows.value = plan
    .filter((p) => p.enabled)
    .map((p) => ({ ...p, correctable: p.taps > 0 }));
}

function setPlanTaps(index, taps) {
  const row = planRows.value.find((r) => r.index === index);
  if (row) row.taps = Math.round(taps / 128) * 128;
}

const planTotal = computed(() => planRows.value.reduce((s, r) => s + r.taps, 0));
const correctedOutputs = computed(() => planRows.value.filter((r) => r.correctable && r.taps > 0));

// Locked in once Plan is confirmed; every later stage reads from this.
const finalizedOutputs = ref([]); // [{index, label, fLo, fHi, taps}]
const bandByOutput = new Map(); // output index -> {fLo, fHi} (analysis band, extended)
const passbandByOutput = new Map(); // output index -> {fLo, fHi} (strict, for design + contradiction check)

function confirmPlan() {
  finalizedOutputs.value = correctedOutputs.value.map((r) => ({ ...r }));
  bandByOutput.clear();
  passbandByOutput.clear();
  for (const o of finalizedOutputs.value) {
    passbandByOutput.set(o.index, { fLo: o.fLo, fHi: o.fHi });
    bandByOutput.set(o.index, analysisBandFor(o));
  }
  stage.value = 'setup';
}

// ===== Stage 1: Setup =====

let recorder = null;
const recording = ref(false);
const micSettings = ref(null);
const captureChain = computed(() => describeCaptureSettings(micSettings.value));

let originalFirEnabled = null;
let originalInputEqEnabled = null;
let flagsDisabled = false;

async function disableMeasurementFlags() {
  if (flagsDisabled) return;
  originalFirEnabled = store.preset.firEnabled;
  originalInputEqEnabled = store.preset.inputEq.enabled;
  flagsDisabled = true;
  if (originalFirEnabled) await store.setFirEnabled(false);
  if (originalInputEqEnabled) await store.setInputEqEnabled(false);
}

async function restoreMeasurementFlags() {
  if (!flagsDisabled) return;
  flagsDisabled = false;
  try {
    if (originalFirEnabled) await store.setFirEnabled(true);
    if (originalInputEqEnabled) await store.setInputEqEnabled(true);
  } catch (e) {
    console.error('Failed to restore FIR/EQ flags after the wizard', e);
  }
}

async function startCapture() {
  errorMessage.value = '';
  recorder = new MicRecorder();
  try {
    await recorder.start();
  } catch (err) {
    recorder = null;
    fail(err?.name === 'NotAllowedError' ? 'Microphone permission denied.' : `Could not open microphone: ${err.message}`);
    return;
  }
  // What the browser actually granted (getUserMedia constraints are a
  // request, not a promise - MicRecorder doesn't expose a settings readback
  // of its own, so this reads the stream it already holds directly, the
  // same way AnalyzerView's own mic capture does).
  try {
    micSettings.value = recorder.stream?.getAudioTracks()[0]?.getSettings?.() ?? null;
  } catch {
    micSettings.value = null;
  }
  recording.value = true;
}

async function beginMeasurement() {
  if (gen.isActive) await gen.stop();
  await disableMeasurementFlags();
  stage.value = 'measure';
  startMeasurement();
}

// ===== Stage 2: Measure =====

const measureSlot = ref(0);
const measureTotalSlots = ref(0);
const measureOutput = ref(null);
let unsubscribeLive = null;
let fallbackTimer = null;
let sweepSchedule = null;
let sweepOrder = null;

const measureLabel = computed(() => {
  if (measureTotalSlots.value === 0) return 'Starting the sweep…';
  const label = measureOutput.value != null ? (store.outputs[measureOutput.value]?.label ?? `Output ${measureOutput.value + 1}`) : '';
  return `Sweep ${measureSlot.value + 1} of ${measureTotalSlots.value} — ${label}`;
});
const measureProgressPct = computed(() =>
  measureTotalSlots.value > 0 ? Math.round(((measureSlot.value + 1) / measureTotalSlots.value) * 100) : 5
);

function cleanupMeasurement() {
  if (unsubscribeLive) { unsubscribeLive(); unsubscribeLive = null; }
  clearTimeout(fallbackTimer);
  fallbackTimer = null;
}

async function startMeasurement() {
  measureSlot.value = 0;
  measureTotalSlots.value = 0;
  measureOutput.value = null;

  unsubscribeLive = apiClient.connectLiveUpdates((msg) => {
    if (msg.messageType !== 'probeEvent' || typeof msg.line !== 'string') return;
    const evt = parseSweepEventLine(msg.line);
    if (!evt) return;
    if (evt.type === 'start') {
      sweepSchedule = evt.schedule;
      sweepOrder = evt.order;
      measureTotalSlots.value = evt.order.length * evt.schedule.nPasses;
      measureOutput.value = evt.order[0] ?? null;
      const durationMs = sweepDurationS(evt.schedule, evt.order.length) * 1000;
      clearTimeout(fallbackTimer);
      fallbackTimer = setTimeout(() => finishMeasurement(), durationMs + 3000);
    } else if (evt.type === 'chirp') {
      measureSlot.value = evt.slot;
      measureOutput.value = evt.output;
    } else if (evt.type === 'done') {
      finishMeasurement();
    } else if (evt.type === 'stop') {
      if (stage.value === 'measure') failMeasurement('The measurement was stopped.');
    } else if (evt.type === 'err') {
      failMeasurement(`The device rejected the sweep (${evt.reason}).`);
    }
  });

  try {
    await apiClient.startSweepProbe({ level: SWEEP_LEVEL, f0: 20, f1: 20000, chirpSamples: 131072, passes: 2 });
  } catch (err) {
    failMeasurement(`Could not start the measurement sweep: ${err.message}`);
  }
}

function failMeasurement(message) {
  cleanupMeasurement();
  recorder?.dispose();
  recorder = null;
  recording.value = false;
  fail(message);
  stage.value = 'setup';
}

function cancelMeasurement() {
  apiClient.stopSweepProbe().catch(() => {});
  failMeasurement('Measurement cancelled.');
}

const measureResult = ref(null);
const predicted = reactive(new Map());
const kernels = new Map();

function finishMeasurement() {
  if (stage.value !== 'measure' || !recorder) return;
  cleanupMeasurement();
  stage.value = 'processing';

  setTimeout(() => {
    const { samples, sampleRate } = recorder.stop();
    recorder = null;
    recording.value = false;
    restoreMeasurementFlags();

    setTimeout(() => {
      try {
        const result = processSweepCapture(samples, sampleRate, sweepSchedule, sweepOrder, {
          bandForOutput: (o) => bandByOutput.get(o) ?? { fLo: 20, fHi: 20000 },
        });
        measureResult.value = { ...result, sampleRate };
        stage.value = 'review';
      } catch (err) {
        fail(`Analysis failed: ${err.message}`);
        stage.value = 'setup';
      }
    }, 50);
  }, 500);
}

// ===== Stage 3: Review =====

const reviewRows = computed(() => {
  if (!measureResult.value) return [];
  return measureResult.value.outputs.map((o) => ({
    output: o.output,
    label: store.outputs[o.output]?.label ?? `Output ${o.output + 1}`,
    measurement: o.measurement,
    snrDb: o.snrDb,
    lowSnr: o.snrDb < LOW_SNR_DB,
    contradiction: passbandContradiction(o.measurement, passbandByOutput.get(o.output) ?? { fLo: 20, fHi: 20000 }),
    band: passbandByOutput.get(o.output),
    harmonics: o.harmonics,
  }));
});

// ===== Stage 4: Design =====

// Only the outputs Plan actually budgeted taps for get a kernel - Review
// still shows every measured output (subs included) for diagnostics, but
// designing/applying a kernel for an output with 0 planned taps makes no
// sense, so Design and Apply both work from this narrower list.
const designRows = computed(() => reviewRows.value.filter((row) => finalizedOutputs.value.some((o) => o.index === row.output)));

const renderChoice = ref('music');
const customBudgetMs = ref(10);
const latencyBudgetMs = computed(() => {
  if (renderChoice.value === 'custom') return customBudgetMs.value;
  return RENDER_PRESETS.find((p) => p.id === renderChoice.value)?.latencyBudgetMs ?? 0;
});
const reachHzValue = computed(() => reachHz(latencyBudgetMs.value, DEVICE_SAMPLE_RATE));
const kernelsReady = ref(false);

// The house curve the kernels aim for. Same selection object the analyzer's
// auto-EQ uses, stored under one key - a target chosen in either place is
// the one the other offers next time.
const target = reactive({ ...DEFAULT_TARGET, customPoints: null, customName: '' });
const targetError = ref('');
const targetHelp = computed(() => targetModeHelp(target));
const targetCurves = reactive(new Map()); // output index -> targetDb on its measured freqs

watch(target, () => writeStoredTarget(target));

// Either knob changes what a kernel would be, so the rendered set stops
// matching the controls above it - drop it rather than leave a prediction
// aimed at the previous settings on screen.
watch([target, latencyBudgetMs], () => { kernelsReady.value = false; });

function onTargetFileSelected(event) {
  targetError.value = '';
  const file = event.target.files?.[0];
  event.target.value = '';
  if (!file) return;
  const reader = new FileReader();
  reader.onload = () => {
    const points = parseCalibrationFile(String(reader.result));
    if (!points) {
      targetError.value = 'No “frequency gain” pairs found in that file.';
      return;
    }
    target.customPoints = points;
    target.customName = file.name;
  };
  reader.readAsText(file);
}

function runDesign() {
  kernels.clear();
  predicted.clear();
  targetCurves.clear();
  for (const row of designRows.value) {
    const output = finalizedOutputs.value.find((o) => o.index === row.output);
    if (!output) continue;
    const kernel = designOutputKernel(row.measurement, output, {
      sampleRate: DEVICE_SAMPLE_RATE,
      latencyBudgetMs: latencyBudgetMs.value,
      target,
    });
    kernels.set(row.output, kernel);
    predicted.set(row.output, predictCorrected(kernel, row.measurement, DEVICE_SAMPLE_RATE));
    targetCurves.set(row.output, targetDbForOutput(row.measurement, output, target));
  }
  kernelsReady.value = true;
}

// ===== Stage 5: Apply =====

const destPresetName = ref('');
const applying = ref(false);
const applyError = ref('');
const appliedPresetName = ref('');

function defaultDestName(source) {
  const suffix = ' 2';
  const trimmed = source.length + suffix.length > 15 ? source.slice(0, 15 - suffix.length) : source;
  return `${trimmed}${suffix}`;
}

// The preset name field's own 15-char limit (docs/AUTO_FIR_CONTRACTS.md
// pins this even though the device's general preset name field allows up
// to 48 - InputGroup has no maxlength prop, so it's enforced here instead).
function setDestPresetName(value) {
  destPresetName.value = String(value).slice(0, 15);
}

async function apply() {
  applying.value = true;
  applyError.value = '';
  try {
    const existingFirFiles = await apiClient.getFirFiles();
    const currentDelaysUs = {};
    for (const o of store.outputs) currentDelaysUs[o.index] = o.delayUs;

    const arrivals = measureResult.value.outputs
      .filter((o) => kernels.has(o.output))
      .map((o) => ({ output: o.output, delayUs: o.delayUs }));
    const relDelays = new Map(computeRelativeDelays(arrivals, currentDelaysUs, MAX_DELAY_US).map((r) => [r.output, r.newDelayUs]));

    const outputs = finalizedOutputs.value
      .filter((o) => kernels.has(o.index))
      .map((o) => ({
        index: o.index,
        label: o.label,
        kernel: kernels.get(o.index),
        delayUs: relDelays.get(o.index) ?? null,
      }));

    const plan = buildApplyPlan({
      sourcePresetName: store.presetName,
      destPresetName: destPresetName.value.trim(),
      existingFirFiles,
      outputs,
    });

    for (const step of plan) {
      await apiClient[step.method](...step.args);
    }

    appliedPresetName.value = destPresetName.value.trim();
    stage.value = 'verify';
  } catch (err) {
    applyError.value = err.message || 'Apply failed.';
  } finally {
    applying.value = false;
  }
}

// ===== Stage 6: Verify (optional) =====

const verifying = ref(false);
const verifyResult = ref(null);
const verifyRows = computed(() => {
  if (!verifyResult.value) return [];
  return verifyResult.value.outputs.map((o) => ({
    output: o.output,
    label: store.outputs[o.output]?.label ?? `Output ${o.output + 1}`,
    before: measureResult.value.outputs.find((m) => m.output === o.output)?.measurement,
    after: o.measurement,
    band: passbandByOutput.get(o.output),
  }));
});

async function runVerify() {
  verifying.value = true;
  errorMessage.value = '';
  try {
    await apiClient.setActivePreset(appliedPresetName.value);
    await store.loadPreset(appliedPresetName.value);

    const hadInputEq = store.preset.inputEq.enabled;
    if (hadInputEq) await store.setInputEqEnabled(false);

    const rec = new MicRecorder();
    await rec.start();

    const schedule = await new Promise((resolve, reject) => {
      const unsub = apiClient.connectLiveUpdates((msg) => {
        if (msg.messageType !== 'probeEvent' || typeof msg.line !== 'string') return;
        const evt = parseSweepEventLine(msg.line);
        if (evt?.type === 'start') { unsub(); resolve(evt); }
        else if (evt?.type === 'err') { unsub(); reject(new Error(evt.reason)); }
      });
      apiClient.startSweepProbe({ level: SWEEP_LEVEL, f0: 20, f1: 20000, chirpSamples: 131072, passes: 2 }).catch((e) => { unsub(); reject(e); });
    });

    await new Promise((resolve) => {
      const unsub = apiClient.connectLiveUpdates((msg) => {
        if (msg.messageType !== 'probeEvent' || typeof msg.line !== 'string') return;
        const evt = parseSweepEventLine(msg.line);
        if (evt?.type === 'done' || evt?.type === 'stop' || evt?.type === 'err') { unsub(); resolve(); }
      });
      setTimeout(() => { unsub(); resolve(); }, sweepDurationS(schedule.schedule, schedule.order.length) * 1000 + 3000);
    });

    await new Promise((r) => setTimeout(r, 500));
    const { samples, sampleRate } = rec.stop();

    if (hadInputEq) await store.setInputEqEnabled(true);

    verifyResult.value = processSweepCapture(samples, sampleRate, schedule.schedule, schedule.order, {
      bandForOutput: (o) => bandByOutput.get(o) ?? { fLo: 20, fHi: 20000 },
    });
  } catch (err) {
    fail(`Re-measure failed: ${err.message}`);
  } finally {
    verifying.value = false;
  }
}

// ===== Lifecycle =====

onMounted(async () => {
  const storedTarget = readStoredTarget();
  if (storedTarget) Object.assign(target, storedTarget);

  // Reached only via the launch button on the active preset's editor page,
  // which keeps the shared preset store pointed at it - a direct/refreshed
  // visit has nothing to measure, so bounce home rather than show a blank page.
  if (!store.presetName) {
    router.push('/');
    return;
  }
  if (!store.preset) {
    await store.loadPreset(store.presetName);
  }
  if (store.preset) {
    destPresetName.value = defaultDestName(store.presetName);
    await loadPlan();
  }
});

function quit() {
  cleanupMeasurement();
  if (stage.value === 'measure' || stage.value === 'processing') {
    apiClient.stopSweepProbe().catch(() => {});
  }
  recorder?.dispose();
  recorder = null;
  restoreMeasurementFlags();
  router.push(`/preset/${encodeURIComponent(store.presetName ?? '')}`);
}

onUnmounted(() => {
  cleanupMeasurement();
  if (stage.value === 'measure' || stage.value === 'processing') {
    apiClient.stopSweepProbe().catch(() => {});
  }
  recorder?.dispose();
  restoreMeasurementFlags();
});
</script>

<style scoped>
@reference "../style.css";

.chip {
  @apply px-3 py-1.5 rounded-full text-sm bg-vybes-dark-element border border-vybes-border text-vybes-text-secondary;
}
.chip-active {
  @apply bg-vybes-dark-input text-vybes-text-primary border-vybes-primary;
}
</style>
