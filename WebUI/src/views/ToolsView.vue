<template>
  <div class="container mx-auto px-0 sm:px-4 py-3">
    <h1 class="text-2xl font-semibold mb-6 px-3 sm:px-0 text-vybes-text-primary">Audio Tools</h1>

    <!-- General Feedback Message Area -->
    <div v-if="toolMessage" class="mb-6 mx-3 sm:mx-0 p-4 rounded-md text-sm text-center transition-all duration-300"
      :class="{
        'bg-green-700 text-green-100': messageType === 'success',
        'bg-red-700 text-red-100': messageType === 'error',
        'bg-blue-700 text-blue-100': messageType === 'info',
      }">
      {{ toolMessage }}
    </div>

    <div class="grid md:grid-cols-1 lg:grid-cols-3 gap-8">
      <!-- Tone Generator Section -->
      <CardSection title="Tone Generator">
        <div class="mb-4">
          <RangeSlider
            v-model="toneFrequency"
            label="Frequency"
            :min="20"
            :max="20000"
            :decimals="0"
            unit=" Hz"
            logarithmic
          />
        </div>
        <div class="mb-6">
          <RangeSlider
            v-model="toneVolume"
            label="Volume"
            :min="1"
            :max="100"
            unit="%"
          />
        </div>
        <button @click="toggleTone" class="w-full"
          :class="isGeneratingTone ? 'btn-danger' : 'btn-primary'">
          <span v-if="isGeneratingTone">Stop Tone</span>
          <span v-else>Start Tone</span>
        </button>
        <p v-if="isGeneratingTone" class="mt-3 text-xs text-green-400 text-center">Tone is currently active.</p>
      </CardSection>

      <!-- Pink Noise Generator Section -->
      <CardSection title="Pink Noise Generator">
        <div class="mb-6">
          <RangeSlider
            v-model="noiseVolume"
            label="Volume (0 to turn off)"
            :min="0"
            :max="100"
            unit="%"
            :disabled="isGeneratingNoise && noiseVolume > 0"
          />
        </div>
        <button @click="togglePinkNoise" class="w-full"
          :class="isGeneratingNoise ? 'btn-danger' : 'btn-primary'">
          <span v-if="isGeneratingNoise">Stop Pink Noise</span>
          <span v-else>Start Pink Noise</span>
        </button>
        <p v-if="isGeneratingNoise" class="mt-3 text-xs text-green-400 text-center">Pink noise is currently active.</p>
      </CardSection>
    </div>
  </div>
</template>

<script setup>
import { ref, inject, watch, onUnmounted } from 'vue';
import CardSection from '../components/shared/CardSection.vue';
import RangeSlider from '../components/shared/RangeSlider.vue';

const apiClient = inject('vybesAPI');

// Reactive data for Tone Generation
const toneFrequency = ref(1000);
const toneVolume = ref(50);
const isGeneratingTone = ref(false);

// Reactive data for Pink Noise Generation
const noiseVolume = ref(50);
const isGeneratingNoise = ref(false);

// General reactive data
const toolMessage = ref('');
const messageType = ref('info'); // 'success', 'error', 'info'

function clearMessage() {
  toolMessage.value = '';
  messageType.value = 'info';
}

// While the tone is playing, follow slider/input changes so the frequency can
// be swept by ear. Throttled so a drag doesn't flood the device.
const TONE_UPDATE_INTERVAL_MS = 150;
let toneUpdateTimer = null;
let toneUpdatePending = false;

watch([toneFrequency, toneVolume], () => {
  if (isGeneratingTone.value) {
    scheduleToneUpdate();
  }
});

function scheduleToneUpdate() {
  if (toneUpdateTimer) {
    toneUpdatePending = true;
    return;
  }
  sendToneUpdate();
  toneUpdateTimer = setTimeout(() => {
    toneUpdateTimer = null;
    if (toneUpdatePending) {
      toneUpdatePending = false;
      scheduleToneUpdate();
    }
  }, TONE_UPDATE_INTERVAL_MS);
}

async function sendToneUpdate() {
  try {
    await apiClient.generateTone(Math.round(toneFrequency.value), toneVolume.value);
  } catch (error) {
    console.error('Tone update failed:', error);
  }
}

onUnmounted(() => {
  if (toneUpdateTimer) {
    clearTimeout(toneUpdateTimer);
  }
});

function toggleTone() {
  if (isGeneratingTone.value) {
    stopToneSignal();
  } else {
    generateToneSignal();
  }
}

async function generateToneSignal() {
  clearMessage();
  if (toneFrequency.value < 20 || toneFrequency.value > 20000) {
    toolMessage.value = 'Tone frequency must be between 20 and 20000 Hz.';
    messageType.value = 'error';
    return;
  }
  if (toneVolume.value < 1 || toneVolume.value > 100) {
    toolMessage.value = 'Tone volume must be between 1 and 100%.';
    messageType.value = 'error';
    return;
  }

  isGeneratingTone.value = true;
  toolMessage.value = 'Starting tone...';
  messageType.value = 'info';

  try {
    if (!apiClient || typeof apiClient.generateTone !== 'function') {
      throw new Error('API client or generateTone method is not available.');
    }
    await apiClient.generateTone(toneFrequency.value, toneVolume.value);
    toolMessage.value = `Tone generating at ${toneFrequency.value} Hz, ${toneVolume.value}% volume.`;
    messageType.value = 'success';
  } catch (error) {
    console.error('Generate Tone failed:', error);
    isGeneratingTone.value = false;
    toolMessage.value = `Failed to start tone: ${error.message}`;
    messageType.value = 'error';
  }
}

async function stopToneSignal() {
  clearMessage();
  try {
    if (!apiClient || typeof apiClient.stopTone !== 'function') {
      throw new Error('API client or stopTone method is not available.');
    }
    await apiClient.stopTone();
    isGeneratingTone.value = false;
    toolMessage.value = 'Tone stopped.';
    messageType.value = 'info';
  } catch (error) {
    console.error('Stop Tone failed:', error);
    // Even if stopping fails, we should reflect the UI state as stopped.
    // The backend might have issues, but the user's intent is to stop.
    isGeneratingTone.value = false; 
    toolMessage.value = `Failed to stop tone: ${error.message}. Tone may still be active on device.`;
    messageType.value = 'error';
  }
}

async function togglePinkNoise() {
  clearMessage();
  try {
    if (!apiClient || typeof apiClient.generateNoise !== 'function') {
      throw new Error('API client or generateNoise method is not available.');
    }

    if (isGeneratingNoise.value) {
      // Currently generating, so stop it
      await apiClient.generateNoise(0);
      isGeneratingNoise.value = false;
      toolMessage.value = 'Pink noise stopped.';
      messageType.value = 'info';
    } else {
      // Not generating, so start it
      if (noiseVolume.value <= 0 || noiseVolume.value > 100) {
        toolMessage.value = 'Noise volume must be between 1 and 100% to start.';
        messageType.value = 'error';
        return;
      }
      await apiClient.generateNoise(noiseVolume.value);
      isGeneratingNoise.value = true;
      toolMessage.value = `Pink noise generating at ${noiseVolume.value}% volume.`;
      messageType.value = 'success';
    }
  } catch (error) {
    console.error('Toggle Pink Noise failed:', error);
    // If it was an attempt to start, it failed, so not generating.
    // If it was an attempt to stop, it failed, so potentially still generating.
    // The UI state should reflect the intended state if possible, or error if uncertain.
    // For simplicity, if an error occurs during toggle, we assume the state change didn't complete.
    toolMessage.value = `Failed to toggle pink noise: ${error.message}`;
    messageType.value = 'error';
    // Revert optimistic UI updates if necessary, or rely on actual device state if available
  }
}

</script>

<style scoped>
input[type="number"]::-webkit-inner-spin-button,
input[type="number"]::-webkit-outer-spin-button {
  -webkit-appearance: none;
  margin: 0;
}
input[type="number"] {
  -moz-appearance: textfield; /* Firefox */
}
</style>
