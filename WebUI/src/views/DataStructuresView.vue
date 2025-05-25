<template>
  <div class="container mx-auto p-6 bg-vybes-dark-element rounded-lg shadow-xl min-h-[calc(100vh-200px)]">
    <h1 class="text-3xl font-bold mb-6 text-vybes-light-blue text-center">Raw API Data Viewer</h1>

    <div class="mb-6 flex justify-center">
      <button @click="loadAllData" :disabled="isLoading" class="btn-primary">
        <span v-if="isLoading" class="flex items-center">
          <svg class="animate-spin -ml-1 mr-3 h-5 w-5 text-white" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
            <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
            <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
          </svg>
          Refreshing...
        </span>
        <span v-else>Refresh All Preset Data</span>
      </button>
    </div>

    <div v-if="errorMessage" class="mb-6 p-4 rounded-md text-sm text-center bg-red-700 text-red-100">
      <p><strong>Error:</strong> {{ errorMessage }}</p>
    </div>

    <div v-if="isLoading && !allPresetDetails" class="text-center py-10">
      <svg class="animate-spin h-10 w-10 text-vybes-primary mx-auto mb-4" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
        <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
        <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
      </svg>
      <p class="text-vybes-text-secondary">Loading all preset configurations...</p>
    </div>

    <div v-if="!isLoading && !errorMessage && (!allPresetDetails || allPresetDetails.length === 0)" class="text-center py-10">
        <p class="text-vybes-text-secondary text-lg">No data to display. Try refreshing.</p>
    </div>
    
    <div v-if="allPresetDetails && allPresetDetails.length > 0" class="bg-vybes-dark-card p-4 rounded-lg shadow-inner">
      <h2 class="text-xl font-semibold mb-3 text-vybes-accent">All Preset Configurations:</h2>
      <pre class="bg-vybes-dark-input text-vybes-text-primary p-4 rounded-md overflow-auto max-h-[600px] text-sm whitespace-pre-wrap break-all">{{ JSON.stringify(allPresetDetails, null, 2) }}</pre>
    </div>

  </div>
</template>

<script setup>
import { ref, onMounted, inject } from 'vue';

const apiClient = inject('vybesAPI');

const allPresetDetails = ref(null); // Using null to distinguish from empty array initially
const isLoading = ref(false);
const errorMessage = ref('');

async function fetchAllPresetsData() {
  if (!apiClient || typeof apiClient.getPresets !== 'function' || typeof apiClient.getPreset !== 'function') {
    throw new Error('API client, getPresets, or getPreset method is not available.');
  }

  const presetsList = await apiClient.getPresets();
  if (!presetsList || presetsList.length === 0) {
    return []; // Return empty array if no presets found
  }

  const details = [];
  // Using Promise.all to fetch details concurrently for efficiency
  const presetPromises = presetsList.map(async (presetSummary) => {
    try {
      const presetData = await apiClient.getPreset(presetSummary.name);
      return { 
        name: presetSummary.name, 
        isCurrent: presetSummary.isCurrent || false, // Ensure isCurrent is present
        configuration: presetData 
      };
    } catch (error) {
      console.error(`Failed to fetch details for preset ${presetSummary.name}:`, error);
      // Return a structure indicating error for this specific preset
      return { name: presetSummary.name, error: `Failed to load: ${error.message}` };
    }
  });
  
  // Wait for all preset detail fetches to complete
  const results = await Promise.all(presetPromises);
  return results;
}

async function loadAllData() {
  isLoading.value = true;
  errorMessage.value = '';
  // Keep existing data while loading if preferred, or clear it:
  // allPresetDetails.value = null; 

  try {
    allPresetDetails.value = await fetchAllPresetsData();
    if (!allPresetDetails.value || allPresetDetails.value.length === 0) {
        errorMessage.value = 'No presets found or failed to load any preset data.';
    }
  } catch (error) {
    console.error('Failed to load all preset data:', error);
    errorMessage.value = `Failed to load data: ${error.message}`;
    allPresetDetails.value = null; // Clear data on significant error
  } finally {
    isLoading.value = false;
  }
}

onMounted(() => {
  loadAllData();
});

</script>

<style scoped>
.btn-primary {
  @apply bg-vybes-primary hover:bg-vybes-primary-hover text-white font-semibold py-2 px-4 rounded-md shadow disabled:opacity-50 disabled:cursor-not-allowed;
}

pre {
  /* Tailwind classes handle most of this, but specific overrides can go here */
  /* e.g., for scrollbar styling if needed beyond browser defaults */
}
</style>
