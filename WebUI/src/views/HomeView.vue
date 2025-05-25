<template>
  <div class="container mx-auto p-6 bg-vybes-dark-element rounded-lg shadow-xl">
    <h1 class="text-3xl font-bold mb-6 text-vybes-light-blue text-center">System Status Overview</h1>

    <div v-if="isLoading && !liveUpdateData" class="text-center py-10">
      <svg class="animate-spin h-8 w-8 text-vybes-primary mx-auto mb-3" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
        <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
        <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
      </svg>
      <p class="text-vybes-text-secondary">Loading initial system status...</p>
    </div>

    <div v-if="errorMessage" class="bg-red-700 text-red-100 p-4 rounded-md mb-6 text-center">
      <p><strong>Error:</strong> {{ errorMessage }}</p>
    </div>

    <div class="grid grid-cols-1 md:grid-cols-2 gap-6 mb-8">
      <div class="bg-vybes-dark-card p-5 rounded-lg shadow-md">
        <h2 class="text-xl font-semibold mb-3 text-vybes-accent">Connectivity</h2>
        <p class="text-vybes-text-primary">
          WebSocket:
          <span :class="isWebSocketConnected ? 'text-green-400' : 'text-red-400'">
            {{ isWebSocketConnected ? 'Connected' : 'Disconnected' }}
          </span>
        </p>
      </div>

      <div class="bg-vybes-dark-card p-5 rounded-lg shadow-md">
        <h2 class="text-xl font-semibold mb-3 text-vybes-accent">Master Volume</h2>
        <div class="w-full bg-vybes-dark-input rounded-full h-6 overflow-hidden">
          <div class="bg-vybes-primary h-6 text-xs font-medium text-blue-100 text-center p-1 leading-none rounded-full" :style="{ width: volumeLevel + '%' }">
            {{ volumeLevel }}%
          </div>
        </div>
         <p class="text-xs text-vybes-text-secondary mt-1 text-center">Current Level: {{ volumeLevel }}%</p>
      </div>
    </div>

    <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-6">
      <div class="status-card">
        <h3 class="status-title">Current Preset</h3>
        <p class="status-value">{{ currentPresetName }}</p>
      </div>
      <div class="status-card">
        <h3 class="status-title">Subwoofer</h3>
        <p class="status-value" :class="subwooferStatus === 'On' ? 'text-green-400' : 'text-red-400'">{{ subwooferStatus }}</p>
      </div>
      <div class="status-card">
        <h3 class="status-title">DSP Processing</h3>
        <p class="status-value" :class="bypassStatus === 'Active' ? 'text-green-400' : 'text-red-400'">{{ bypassStatus }}</p>
      </div>
      <div class="status-card">
        <h3 class="status-title">Mute Status</h3>
        <p class="status-value" :class="muteStatus === 'Unmuted' ? 'text-green-400' : 'text-red-400'">{{ muteStatus }}</p>
      </div>
    </div>

    <!-- Placeholder for future control buttons -->
    <!-- 
    <div class="mt-8 text-center">
      <h2 class="text-xl font-semibold mb-4 text-vybes-light-blue">Quick Controls</h2>
      <div class="flex justify-center space-x-4">
        <button @click="toggleMute" class="btn-control">Toggle Mute</button>
        <button @click="toggleBypass" class="btn-control">Toggle Bypass</button>
        <button @click="toggleSubwoofer" class="btn-control">Toggle Subwoofer</button>
      </div>
    </div>
    -->

  </div>
</template>

<script setup>
import { ref, onMounted, inject, watch } from 'vue';

const apiClient = inject('vybesAPI');
const liveUpdateData = inject('liveUpdateData');
const isWebSocketConnected = inject('isWebSocketConnected');

const currentPresetName = ref('Loading...');
const subwooferStatus = ref('Loading...');
const bypassStatus = ref('Loading...');
const muteStatus = ref('Loading...');
const volumeLevel = ref(0);
const isLoading = ref(true);
const errorMessage = ref('');

async function fetchStatusData() {
  isLoading.value = true;
  errorMessage.value = '';
  try {
    if (!apiClient) {
        errorMessage.value = 'API Client not available.';
        isLoading.value = false;
        return;
    }
    // Attempt to get the list of presets to find the active one.
    // This is a fallback or initial load strategy. WebSocket is primary.
    const presets = await apiClient.getPresets();
    if (presets && presets.length > 0) {
      const activePreset = presets.find(p => p.is_active); // Assuming an 'is_active' flag or similar
      if (activePreset) {
        currentPresetName.value = activePreset.name;
      } else if (presets.length === 1) { // Or if there's a default/first preset concept
        currentPresetName.value = presets[0].name; // Fallback: use first preset if no active one marked
      } else {
        currentPresetName.value = 'No active preset found';
      }
    } else {
      currentPresetName.value = 'No presets available';
    }
  } catch (error) {
    console.error('Failed to fetch initial status data:', error);
    errorMessage.value = `Failed to load initial data: ${error.message}`;
    currentPresetName.value = 'Error loading preset';
  } finally {
    // isLoading.value = false; // WebSocket will manage loading state primarily
  }
}

onMounted(() => {
  fetchStatusData(); // Fetch initial data, primarily preset name

  // Watch for WebSocket updates to populate most status fields
  watch(liveUpdateData, (newData) => {
    if (newData) {
      isLoading.value = false; // Data has arrived
      currentPresetName.value = newData.currentPresetName || newData.currentPreset || 'N/A';
      subwooferStatus.value = newData.subwooferOn ? 'On' : 'Off';
      bypassStatus.value = newData.dspBypassed ? 'Bypassed' : 'Active'; // Active means DSP is running
      muteStatus.value = newData.isMuted ? 'Muted' : 'Unmuted';
      volumeLevel.value = newData.masterVolume !== undefined ? newData.masterVolume : 0;

      // If specific fields are missing, reflect that
      if (newData.currentPresetName === undefined && newData.currentPreset === undefined) currentPresetName.value = 'Preset data missing';
      if (newData.subwooferOn === undefined) subwooferStatus.value = 'Subwoofer data missing';
      if (newData.dspBypassed === undefined) bypassStatus.value = 'DSP status missing';
      if (newData.isMuted === undefined) muteStatus.value = 'Mute status missing';
      if (newData.masterVolume === undefined) volumeLevel.value = 'Volume data missing';

    } else if (!isWebSocketConnected.value) {
        // If liveUpdateData is null AND websocket is not connected, it might mean a connection issue
        // isLoading.value = false; // No longer loading, but data might be stale or unavailable
        errorMessage.value = "WebSocket is disconnected. Displayed data might be stale or incomplete.";
    }
  }, { immediate: true, deep: true }); // immediate: true to run on mount with current liveUpdateData
});

// Placeholder for control functions if buttons were added
// async function toggleMute() { try { await apiClient.toggleMute(); } catch(e) { console.error(e); errorMessage.value = e.message; }}
// async function toggleBypass() { try { await apiClient.toggleBypass(); } catch(e) { console.error(e); errorMessage.value = e.message; }}
// async function toggleSubwoofer() { try { await apiClient.toggleSubwoofer(); } catch(e) { console.error(e); errorMessage.value = e.message; }}

</script>

<style scoped>
.status-card {
  @apply bg-vybes-dark-card p-5 rounded-lg shadow-md text-center;
}
.status-title {
  @apply text-lg font-semibold text-vybes-accent mb-2;
}
.status-value {
  @apply text-2xl font-light text-vybes-text-primary;
}

/* Placeholder for button styles if controls are added */
/*
.btn-control {
  @apply bg-vybes-primary hover:bg-vybes-primary-hover text-white font-semibold py-2 px-4 rounded-lg shadow transition-colors duration-150 ease-in-out;
}
.btn-control:disabled {
  @apply bg-vybes-light-blue opacity-50 cursor-not-allowed;
}
*/
</style>
