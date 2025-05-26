<template>
  <div class="container mx-auto p-6 bg-vybes-dark-element rounded-lg shadow-xl">
    <h1 class="text-3xl font-bold mb-6 text-vybes-light-blue text-center">Device Calibration</h1>

    <div class="max-w-md mx-auto">
      <p class="mb-4 text-vybes-text-secondary text-center">
        Set the Sound Pressure Level (SPL) for calibration. This value will be used as a reference by the device.
      </p>

      <div class="mb-6">
        <label for="splValue" class="block text-sm font-medium text-vybes-text-primary mb-1">
          Reference SPL (dB):
        </label>
        <input
          type="number"
          id="splValue"
          v-model.number="splValue"
          min="40"
          max="120"
          class="w-full px-4 py-2 bg-vybes-dark-input border border-vybes-dark-border rounded-md shadow-sm focus:ring-vybes-primary focus:border-vybes-primary text-vybes-text-primary placeholder-vybes-text-secondary"
          placeholder="Enter SPL value (e.g., 85)"
        />
      </div>

      <button
        @click="setCalibration"
        :disabled="isLoading"
        class="w-full py-2 px-4 rounded-md font-semibold text-white transition-colors duration-150 ease-in-out"
        :class="{
          'bg-vybes-primary hover:bg-vybes-primary-hover focus:ring-vybes-primary': !isLoading,
          'bg-vybes-light-blue cursor-not-allowed opacity-75': isLoading,
        }"
      >
        <span v-if="isLoading">
          <svg class="animate-spin -ml-1 mr-3 h-5 w-5 text-white inline" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
            <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
            <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
          </svg>
          Calibrating...
        </span>
        <span v-else>Set Calibration</span>
      </button>

      <div v-if="calibrationMessage" class="mt-6 p-4 rounded-md text-sm text-center" :class="messageType === 'success' ? 'bg-green-700 text-green-100' : 'bg-red-700 text-red-100'">
        {{ calibrationMessage }}
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, inject } from 'vue';

const apiClient = inject('vybesAPI');

const splValue = ref(85); // Default SPL value
const calibrationMessage = ref('');
const messageType = ref(''); // 'success' or 'error'
const isLoading = ref(false);

async function setCalibration() {
  isLoading.value = true;
  calibrationMessage.value = '';
  messageType.value = '';

  if (splValue.value === null || splValue.value === undefined || splValue.value < 40 || splValue.value > 120) {
    calibrationMessage.value = 'SPL value must be between 40 and 120 dB.';
    messageType.value = 'error';
    isLoading.value = false;
    return;
  }

  try {
    // Ensure apiClient is available
    if (!apiClient) {
      throw new Error("API client is not available. Check App.vue provide('vybesAPI').");
    }
    
    // Ensure the calibrate method exists on the apiClient
    if (typeof apiClient.calibrate !== 'function') {
        throw new Error("API client does not have a calibrate method. Check your API client implementation in useVybesAPI.js.");
    }

    const response = await apiClient.calibrate(splValue.value);
    // Assuming the API returns a success message or specific structure
    // For example, if response is { success: true, message: "Details..." }
    // Or just a simple string message for success.
    // Adjust based on actual API response.
    if (response && (response.success || typeof response === 'string')) {
        calibrationMessage.value = typeof response === 'string' ? response : (response.message || 'Calibration set successfully to ' + splValue.value + ' dB.');
        messageType.value = 'success';
    } else {
        // Handle cases where response might not explicitly indicate success as expected
        // or if it's an object without a clear success flag/message.
        calibrationMessage.value = 'Calibration completed. Device responded but with unclear status. SPL set to ' + splValue.value + ' dB.';
        messageType.value = 'success'; // Or 'error' if this state is unexpected
    }
  } catch (error) {
    console.error('Calibration API call failed:', error);
    calibrationMessage.value = `Calibration failed: ${error.message || 'Unknown error'}. Please ensure the backend is running and reachable.`;
    messageType.value = 'error';
  } finally {
    isLoading.value = false;
  }
}
</script>

<style scoped>
/* Additional component-specific styles can be added here if Tailwind isn't enough */
/* Example: Custom spinner animation if not using Tailwind's animate-spin */
input[type="number"]::-webkit-inner-spin-button,
input[type="number"]::-webkit-outer-spin-button {
  -webkit-appearance: none;
  margin: 0;
}
input[type="number"] {
  -moz-appearance: textfield; /* Firefox */
}
</style>
