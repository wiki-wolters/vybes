<template>
  <div v-if="modelValue" class="wizard-backdrop" @click.self="onCancel"></div>
  <div v-if="modelValue" class="wizard-content" role="dialog" aria-modal="true" aria-label="Auto-align speaker delays">
    <h3 class="text-xl font-semibold mb-1">Auto-align speaker delays</h3>

    <!-- Intro -->
    <div v-if="step === 'intro'">
      <p class="text-sm text-vybes-text-secondary mb-3">
        The device plays a short sweep through each speaker while your
        phone's microphone listens, then the delays are set so every
        speaker's sound arrives at the same moment.
      </p>
      <ul class="text-sm text-vybes-text-secondary list-disc pl-5 space-y-1 mb-3">
        <li>Hold this device at your listening position and keep it still.</li>
        <li>Keep the room quiet for about {{ Math.round(expectedDurationS) }} seconds.</li>
        <li>Sweeps play at a fixed level, independent of your volume setting.</li>
      </ul>
      <p v-if="!micSupported" class="text-sm text-red-400 mb-3">
        Microphone capture isn't available here. Open the UI over HTTPS
        (required by browsers for mic access) and try again.
      </p>
      <div class="flex justify-end space-x-3 mt-4">
        <button class="btn-secondary" @click="close">Cancel</button>
        <button class="btn-primary" :disabled="!micSupported" @click="startMeasurement">Start</button>
      </div>
    </div>

    <!-- Measuring -->
    <div v-else-if="step === 'measuring'">
      <p class="text-sm text-vybes-text-secondary mb-3">{{ progressLabel }}</p>
      <div class="h-2 rounded bg-vybes-dark-input overflow-hidden mb-4">
        <div class="h-full bg-vybes-primary transition-all duration-500" :style="{ width: `${progressPct}%` }"></div>
      </div>
      <div class="flex justify-end mt-4">
        <button class="btn-secondary" @click="onCancel">Cancel</button>
      </div>
    </div>

    <!-- Analyzing -->
    <div v-else-if="step === 'analyzing'">
      <p class="text-sm text-vybes-text-secondary mb-4">Analyzing the recording…</p>
    </div>

    <!-- Results -->
    <div v-else-if="step === 'results'">
      <p class="text-sm text-vybes-text-secondary mb-3">
        Arrival offsets measured at the listening position:
      </p>
      <div class="overflow-x-auto">
        <table class="w-full text-sm mb-2">
          <thead>
            <tr class="text-left text-vybes-text-secondary border-b border-vybes-border">
              <th class="py-1.5 pr-2 font-medium">Output</th>
              <th class="py-1.5 pr-2 font-medium text-right">Offset</th>
              <th class="py-1.5 pr-2 font-medium text-right">New delay</th>
              <th class="py-1.5 font-medium text-right">Signal</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="row in resultRows" :key="row.output" class="border-b border-vybes-border/50">
              <td class="py-1.5 pr-2">{{ row.label }}</td>
              <td class="py-1.5 pr-2 text-right tabular-nums">
                <template v-if="row.measured">
                  {{ (row.offsetUs / 1000).toFixed(2) }} ms
                  <span class="text-vybes-text-secondary">({{ usToCm(row.offsetUs).toFixed(0) }} cm)</span>
                </template>
                <span v-else class="text-vybes-text-secondary">not detected</span>
              </td>
              <td class="py-1.5 pr-2 text-right tabular-nums">
                <template v-if="row.newDelayUs !== null">{{ (row.newDelayUs / 1000).toFixed(2) }} ms</template>
                <span v-else>—</span>
              </td>
              <td class="py-1.5 text-right">
                <span :class="row.measured ? (row.strong ? 'text-vybes-live' : 'text-yellow-400') : 'text-red-400'">
                  {{ row.measured ? (row.strong ? 'good' : 'weak') : '—' }}
                </span>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
      <p v-for="w in warnings" :key="w" class="text-xs text-yellow-400 mb-1">{{ w }}</p>
      <div class="flex justify-end space-x-3 mt-4">
        <button class="btn-secondary" @click="close">Discard</button>
        <button class="btn-secondary" @click="startMeasurement">Re-measure</button>
        <button class="btn-primary" :disabled="!canApply" @click="applyResults">Apply delays</button>
      </div>
    </div>

    <!-- Error -->
    <div v-else-if="step === 'error'">
      <p class="text-sm text-red-400 mb-3">{{ errorMessage }}</p>
      <div class="flex justify-end space-x-3 mt-4">
        <button class="btn-secondary" @click="close">Close</button>
        <button class="btn-primary" :disabled="!micSupported" @click="startMeasurement">Try again</button>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onUnmounted, watch } from 'vue';
import apiClient from '../api-client.js';
import { usePresetStore } from '../stores/preset.js';
import { useGeneratorStore } from '../stores/generator.js';
import { MicRecorder, micSupported } from '../audio-capture.js';
import { analyzeRecording, usToCm, recordingDurationS } from '../delay-align.js';

const MAX_DELAY_US = 20000;
const PROBE_LEVEL = 50;
// Forward/reverse passes disagreeing by more than this suggests echoes,
// noise or a moving phone - surface it rather than silently averaging.
const CONSISTENCY_WARN_US = 500;
const STRONG_CONFIDENCE = 8;

const props = defineProps({
  modelValue: { type: Boolean, required: true },
});
const emit = defineEmits(['update:modelValue']);

const store = usePresetStore();
const gen = useGeneratorStore();

const step = ref('intro');
const errorMessage = ref('');
const progressSlot = ref(0);
const totalSlots = ref(0);
const currentOutput = ref(null);
const analysis = ref(null);

let recorder = null;
let unsubscribeLive = null;
let fallbackTimer = null;
let schedule = null;

// Rough sequence length for the intro copy, before we have a schedule:
// assume the device defaults (44.1k, ~1.1s spacing, 2 chirps per output).
const expectedDurationS = computed(() => {
  const n = 2 * store.enabledOutputs.length;
  return 1.5 + n * 1.115 + 1;
});

const progressPct = computed(() =>
  totalSlots.value > 0 ? Math.round(((progressSlot.value + 1) / totalSlots.value) * 100) : 5
);
const progressLabel = computed(() => {
  if (totalSlots.value === 0) return 'Starting the probe…';
  const label = currentOutput.value !== null
    ? store.outputs[currentOutput.value]?.label ?? `Output ${currentOutput.value + 1}`
    : '';
  return `Playing sweep ${progressSlot.value + 1} of ${totalSlots.value} — ${label}`;
});

const resultRows = computed(() => {
  if (!analysis.value) return [];
  return analysis.value.channels.map((c) => ({
    ...c,
    label: store.outputs[c.output]?.label ?? `Output ${c.output + 1}`,
    strong: c.confidence >= STRONG_CONFIDENCE,
  }));
});

const canApply = computed(
  () => analysis.value && analysis.value.channels.some((c) => c.newDelayUs !== null)
);

const warnings = computed(() => {
  if (!analysis.value) return [];
  const out = [];
  const rows = analysis.value.channels;
  if (rows.some((c) => c.clamped)) {
    out.push('Some offsets exceed the 20 ms delay range and were capped - check speaker distances.');
  }
  if (rows.some((c) => !c.measured)) {
    out.push('Some outputs were not detected - check their wiring/routing, or re-measure closer to them.');
  }
  if (rows.some((c) => c.measured && c.consistencyUs !== null && c.consistencyUs > CONSISTENCY_WARN_US)) {
    out.push('The two measurement passes disagree - keep the phone still and the room quiet, then re-measure.');
  }
  const measured = rows.filter((c) => c.measured);
  if (measured.length === 1) {
    out.push('Only one output was detected, so no alignment can be computed.');
  }
  return out;
});

function cleanupMeasurement() {
  if (unsubscribeLive) {
    unsubscribeLive();
    unsubscribeLive = null;
  }
  clearTimeout(fallbackTimer);
  fallbackTimer = null;
  if (recorder) {
    recorder.dispose();
    recorder = null;
  }
}

function failWith(message) {
  cleanupMeasurement();
  errorMessage.value = message;
  step.value = 'error';
}

async function startMeasurement() {
  analysis.value = null;
  progressSlot.value = 0;
  totalSlots.value = 0;
  currentOutput.value = null;

  // The probe measures the chain as configured, so delays must actually be
  // applied during it; the tone/noise generator would contaminate the
  // recording (the device silences it too - keep the UI in sync).
  if (gen.isActive) await gen.stop();
  if (!store.preset.delaysEnabled) store.setDelaysEnabled(true);

  recorder = new MicRecorder();
  try {
    await recorder.start();
  } catch (err) {
    recorder = null;
    failWith(err?.name === 'NotAllowedError'
      ? 'Microphone permission denied.'
      : `Could not open microphone: ${err.message}`);
    return;
  }

  step.value = 'measuring';

  unsubscribeLive = apiClient.connectLiveUpdates((msg) => {
    if (msg.messageType !== 'probeEvent' || typeof msg.line !== 'string') return;
    const parts = msg.line.split(' ');
    if (parts[0] === 'CHIRP') {
      progressSlot.value = Number(parts[1]) || 0;
      currentOutput.value = Number(parts[2]);
    } else if (parts[0] === 'DONE') {
      finishMeasurement();
    } else if (parts[0] === 'STOP') {
      // Someone else stopped it (or our own cancel raced) - treat as abort
      if (step.value === 'measuring') failWith('The probe was stopped.');
    } else if (parts[0] === 'ERR') {
      failWith(`The probe failed on the device (${parts.slice(1).join(' ')}).`);
    }
  });

  try {
    schedule = await apiClient.startDelayProbe(PROBE_LEVEL);
  } catch (err) {
    failWith(`Could not start the probe: ${err.message}`);
    return;
  }
  totalSlots.value = schedule.order.length;
  currentOutput.value = schedule.order[0];

  // Fallback if the DONE event is missed (websocket hiccup): the schedule
  // tells us exactly how long the sequence runs.
  const durationMs = recordingDurationS(schedule, schedule.order.length) * 1000;
  fallbackTimer = setTimeout(() => finishMeasurement(), durationMs + 2000);
}

function finishMeasurement() {
  if (step.value !== 'measuring' || !recorder) return;
  step.value = 'analyzing';

  // Half a second of post-roll so the last chirp's tail is fully captured
  setTimeout(() => {
    if (!recorder) return; // cancelled during the post-roll
    const { samples, sampleRate } = recorder.stop();
    cleanupMeasurement();

    // Let the spinner paint before the FFT crunch blocks the main thread
    setTimeout(() => {
      try {
        const currentDelaysUs = store.outputs.map((o) => o.delayUs);
        analysis.value = analyzeRecording(samples, sampleRate, schedule, currentDelaysUs, MAX_DELAY_US);
        step.value = 'results';
      } catch (err) {
        failWith(`Analysis failed: ${err.message}`);
      }
    }, 50);
  }, 500);
}

function applyResults() {
  for (const c of analysis.value.channels) {
    if (c.newDelayUs !== null) {
      store.setOutputDelay(c.output, c.newDelayUs);
    }
  }
  close();
}

function onCancel() {
  if (step.value === 'measuring' || step.value === 'analyzing') {
    apiClient.stopDelayProbe().catch(() => {});
  }
  close();
}

function close() {
  cleanupMeasurement();
  step.value = 'intro';
  emit('update:modelValue', false);
}

watch(() => props.modelValue, (open) => {
  if (open) step.value = 'intro';
  else cleanupMeasurement();
});

onUnmounted(() => {
  if (step.value === 'measuring') apiClient.stopDelayProbe().catch(() => {});
  cleanupMeasurement();
});
</script>

<style scoped>
/* Same shell as ModalDialog.vue - this dialog owns its footer buttons per
   step, so it can't reuse that component's fixed Cancel/Confirm pair. */
.wizard-backdrop {
  position: fixed;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  background-color: rgba(0, 0, 0, 0.5);
  z-index: 50;
}

.wizard-content {
  background-color: var(--vybes-dark-card);
  border: 1px solid var(--vybes-border);
  border-radius: 0.5rem;
  padding: 1.5rem;
  max-width: 560px;
  width: 90%;
  max-height: 90vh;
  overflow-y: auto;
  box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.5);
  z-index: 51;
  position: fixed;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
}
</style>
