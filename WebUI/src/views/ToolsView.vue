<template>
  <div class="container mx-auto p-6 bg-vybes-dark-element rounded-lg shadow-xl">
    <h1 class="text-3xl font-bold mb-8 text-vybes-light-blue text-center">Audio Tools</h1>

    <!-- General Feedback Message Area -->
    <div v-if="toolMessage" class="mb-6 p-4 rounded-md text-sm text-center transition-all duration-300"
      :class="{
        'bg-green-700 text-green-100': messageType === 'success',
        'bg-red-700 text-red-100': messageType === 'error',
        'bg-blue-700 text-blue-100': messageType === 'info',
      }">
      {{ toolMessage }}
    </div>

    <div class="grid md:grid-cols-1 lg:grid-cols-3 gap-8">
      <!-- Tone Generator Section -->
      <!-- <CardSection title="Tone Generator">
        <div class="mb-4">
          <InputGroup
            v-model.number="toneFrequency"
            label="Frequency (10 - 20000 Hz):"
            type="number"
            :min="10"
            :max="20000"
            :disabled="isGeneratingTone"
          />
        </div>
        <div class="mb-6">
          <RangeSlider
            v-model="toneVolume"
            label="Volume"
            :min="1"
            :max="100"
            unit="%"
            :disabled="isGeneratingTone"
          />
        </div>
        <div class="flex space-x-3">
          <button @click="generateToneSignal" :disabled="isGeneratingTone" class="btn-primary flex-1">
            <span v-if="isGeneratingTone">Generating...</span>
            <span v-else>Start Tone</span>
          </button>
          <button @click="stopToneSignal" :disabled="!isGeneratingTone" class="btn-secondary flex-1">
            Stop Tone
          </button>
        </div>
        <p v-if="isGeneratingTone" class="mt-3 text-xs text-green-400 text-center">Tone is currently active.</p>
      </CardSection> -->

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

      <!-- Test Pulse Section -->
      <!-- <CardSection title="System Test Pulse">
        <p class="text-sm text-vybes-text-secondary mb-4">
          Plays a short audio pulse through the system. Useful for testing connections and basic output.
        </p>
        <button @click="playTestPulse" :disabled="isLoading" class="btn-primary w-full">
          <span v-if="isLoading" class="flex items-center justify-center">
            <svg class="animate-spin -ml-1 mr-3 h-5 w-5 text-white" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
              <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
              <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
            </svg>
            Playing...
          </span>
          <span v-else>Play Test Pulse</span>
        </button>
      </CardSection> -->
    </div>
  </div>
</template>

<script setup>
import { ref, inject } from 'vue';
import CardSection from '../components/shared/CardSection.vue';
import InputGroup from '../components/shared/InputGroup.vue';
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
const isLoading = ref(false); // For specific actions like pulse
const toolMessage = ref('');
const messageType = ref('info'); // 'success', 'error', 'info'

function clearMessage() {
  toolMessage.value = '';
  messageType.value = 'info';
}

async function generateToneSignal() {
  clearMessage();
  if (toneFrequency.value < 10 || toneFrequency.value > 20000) {
    toolMessage.value = 'Tone frequency must be between 10 and 20000 Hz.';
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
    if (!apiClient || typeof apiClient.generateTone !== 'function') {
      throw new Error('API client or generateTone method is not available.');
    }
    // Assuming 0 volume stops the tone, or a dedicated endpoint like apiClient.stopTone()
    await apiClient.generateTone(toneFrequency.value, 0); 
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

async function playTestPulse() {
  clearMessage();
  isLoading.value = true;
  toolMessage.value = 'Playing test pulse...';
  messageType.value = 'info';

  try {
    if (!apiClient || typeof apiClient.playPulse !== 'function') {
      throw new Error('API client or playPulse method is not available.');
    }
    await apiClient.playPulse();
    toolMessage.value = 'Test pulse played successfully.';
    messageType.value = 'success';
  } catch (error) {
    console.error('Play Test Pulse failed:', error);
    toolMessage.value = `Failed to play test pulse: ${error.message}`;
    messageType.value = 'error';
  } finally {
    isLoading.value = false;
  }
}

</script>

<style scoped>
@reference "../style.css";

.tool-card {
  @apply p-8 rounded-lg shadow-lg transition-all duration-300 ease-in-out hover:shadow-2xl;
  /* Consider adding hover effects or subtle gradients if desired */
}

.tool-input {
  @apply w-full px-4 py-2 bg-vybes-dark-input border border-vybes-dark-border rounded-md shadow-sm focus:ring-vybes-primary focus:border-vybes-primary text-vybes-text-primary placeholder-vybes-text-secondary disabled:opacity-50 disabled:cursor-not-allowed;
}

/* Basic button styles - can be expanded or use global button styles if defined */
.btn-primary {
  @apply bg-vybes-primary hover:bg-vybes-primary-hover text-white font-semibold py-2 px-4 rounded-md shadow transition-colors duration-150 ease-in-out disabled:opacity-50 disabled:cursor-not-allowed;
}

.btn-secondary {
  @apply bg-vybes-accent hover:bg-vybes-accent-hover text-white font-semibold py-2 px-4 rounded-md shadow transition-colors duration-150 ease-in-out disabled:opacity-50 disabled:cursor-not-allowed;
}

.btn-danger {
  @apply bg-red-600 hover:bg-red-700 text-white font-semibold py-2 px-4 rounded-md shadow transition-colors duration-150 ease-in-out disabled:opacity-50 disabled:cursor-not-allowed;
}

input[type="range"]::-webkit-slider-thumb {
  -webkit-appearance: none;
  appearance: none;
  width: 20px;
  height: 20px;
  background: #4F46E5; /* vybes-primary */
  cursor: pointer;
  border-radius: 50%;
  border: 2px solid white;
}

input[type="range"]::-moz-range-thumb {
  width: 18px;
  height: 18px;
  background: #4F46E5; /* vybes-primary */
  cursor: pointer;
  border-radius: 50%;
  border: 2px solid white;
}

input[type="number"]::-webkit-inner-spin-button,
input[type="number"]::-webkit-outer-spin-button {
  -webkit-appearance: none;
  margin: 0;
}
input[type="number"] {
  -moz-appearance: textfield; /* Firefox */
}
</style>
