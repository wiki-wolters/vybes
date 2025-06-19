<template>
  <div class="container mx-auto p-6 space-y-6">
    <!-- Loading State -->
    <div v-if="isLoading" class="text-center py-10">
      <svg class="animate-spin h-8 w-8 text-vybes-primary mx-auto mb-3" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
        <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
        <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
      </svg>
      <p class="text-vybes-text-secondary">Loading system...</p>
    </div>

    <!-- Error Message -->
    <div v-if="errorMessage" class="bg-red-700 text-red-100 p-4 rounded-lg mb-6">
      <p><strong>Error:</strong> {{ errorMessage }}</p>
    </div>

    <!-- Main Content -->
    <div v-if="!isLoading" class="space-y-6">
      <!-- Presets Section -->
      <CardSection title="Presets">
        <div class="flex flex-wrap gap-3">
          <button
            v-for="preset in presets"
            :key="preset.name"
            @click="setActivePreset(preset.name)"
            :class="[
              'preset-button',
              preset.isCurrent ? 'preset-active' : 'preset-inactive'
            ]"
          >
            {{ preset.name }}
            
            <!-- Active preset controls -->
            <div v-if="preset.isCurrent" class="preset-controls" @click.stop>
              <button
                @click="editPreset(preset.name)"
                class="preset-control-btn"
                title="Edit preset"
              >
                <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M11 5H6a2 2 0 00-2 2v11a2 2 0 002 2h11a2 2 0 002-2v-5m-1.414-9.414a2 2 0 112.828 2.828L11.828 15H9v-2.828l8.586-8.586z"></path>
                </svg>
              </button>
            </div>
          </button>
          
          <!-- Add New Preset Button -->
          <button
            @click="showNewPresetDialog = true"
            class="preset-button preset-add"
          >
            <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 4v16m8-8H4"></path>
            </svg>
          </button>
        </div>
      </CardSection>

      <!-- Subwoofer Control -->
      <CardSection title="Speakers">
        <div class="flex justify-between gap-3">
          <div class="switch-container">
            <ToggleSwitch v-model="subwooferEnabled" @change="toggleSubwoofer" label="Subwoofer"/>
          </div>

          <div class="switch-container">
            <ToggleSwitch v-model="leftEnabled" @change="toggleLeft" label="Left" />
          </div>

          <div class="switch-container">
            <ToggleSwitch v-model="rightEnabled" @change="toggleRight" label="Right" />
          </div>
        </div>
      </CardSection>

      <CardSection title="Mute">
        <div class="space-y-4">
          <!-- Mute Percentage Slider -->
          <RangeSlider
            v-model="mutePercentage"
            label="Volume reduction"
            :min="1"
            :max="100"
            unit="%"
            @update:modelValue="updateMutePercentage"
          />
          
          <!-- Mute On/Off Switch -->
          <ToggleSwitch v-model="muteEnabled" @change="toggleMute" label="Mute"/>
        </div>
      </CardSection>




    </div>

    <!-- New Preset Dialog -->
    <ModalDialog
      v-model="showNewPresetDialog"
      title="Create New Preset"
      confirmText="Create"
      @confirm="createNewPreset"
    >
      <InputGroup
        ref="newPresetNameInput"
        v-model="newPresetName"
        placeholder="Enter preset name"
        @keyup.enter="createNewPreset"
      />
    </ModalDialog>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted, watch, nextTick } from 'vue';
import { useRouter } from 'vue-router';
import apiClient from '../api-client.js';
import CardSection from '../components/shared/CardSection.vue';
import InputGroup from '../components/shared/InputGroup.vue';
import RangeSlider from '../components/shared/RangeSlider.vue';
import ModalDialog from '../components/shared/ModalDialog.vue';
import ToggleSwitch from '../components/shared/ToggleSwitch.vue';

const router = useRouter();

// State
const isLoading = ref(true);
const errorMessage = ref('');
const presets = ref([]);
const subwooferEnabled = ref(false);
const muteEnabled = ref(false);
const mutePercentage = ref(100);
let muteUpdateTimeout = null;
const calibrationValue = ref(null);
const showNewPresetDialog = ref(false);
const newPresetName = ref('');
const newPresetNameInput = ref(null);

// Load initial data
async function loadSystemData() {
  try {
    isLoading.value = true;
    errorMessage.value = '';

    // Load presets
    const presetsData = await apiClient.getPresets();
    presets.value = presetsData || [];

    // Load system status (you'll need to add this endpoint)
    try {
      const status = await apiClient.getStatus();
      subwooferEnabled.value = status.subwoofer === 'on';
      leftEnabled.value = status.left === 'on';
      rightEnabled.value = status.right === 'on';
      muteEnabled.value = status.mute === 'on';
      mutePercentage.value = status.mutePercent || 100;
    } catch (statusError) {
      console.warn('Could not load system status:', statusError);
    }

    // Load calibration data
    try {
      const calibration = await apiClient.getCalibration();
      calibrationValue.value = calibration.spl;
    } catch (calibrationError) {
      console.warn('Could not load calibration data:', calibrationError);
    }

  } catch (error) {
    console.error('Failed to load system data:', error);
    errorMessage.value = `Failed to load system data: ${error.message}`;
  } finally {
    isLoading.value = false;
  }
}

// Preset management
async function setActivePreset(presetName) {
  try {
    await apiClient.setActivePreset(presetName);
    // Update local state
    presets.value = presets.value.map(p => ({
      ...p,
      isCurrent: p.name === presetName
    }));
  } catch (error) {
    console.error('Failed to set active preset:', error);
    errorMessage.value = `Failed to activate preset: ${error.message}`;
  }
}

function editPreset(presetName) {
  router.push(`/preset/${encodeURIComponent(presetName)}`);
}

async function createNewPreset() {
  if (!newPresetName.value.trim()) return;
  
  try {
    await apiClient.createPreset(newPresetName.value.trim());

    //Navigate to preset editor
    router.push(`/preset/${encodeURIComponent(newPresetName.value.trim())}`);
    
    // Reload presets
    const updatedPresets = await apiClient.getPresets();
    presets.value = updatedPresets || [];
    
    // Close dialog and reset name
    showNewPresetDialog.value = false;
    newPresetName.value = '';
    
  } catch (error) {
    console.error('Failed to create preset:', error);
    errorMessage.value = `Failed to create preset: ${error.message}`;
  }
}

// System controls
async function toggleSubwoofer() {
  try {
    await apiClient.setSubwoofer(subwooferEnabled.value);
  } catch (error) {
    console.error('Failed to toggle subwoofer:', error);
    errorMessage.value = `Failed to toggle subwoofer: ${error.message}`;
    // Revert state on error
    subwooferEnabled.value = !subwooferEnabled.value;
  }
}

async function toggleMute() {
  try {
    // Send the new state to the API
    await apiClient.setMute(muteEnabled.value);
  } catch (error) {
    console.error('Failed to toggle mute:', error);
    errorMessage.value = `Failed to toggle mute: ${error.message}`;
    // Revert state on error
    muteEnabled.value = !newState;
  }
}

function updateMutePercentage() {
  if (muteUpdateTimeout) {
    clearTimeout(muteUpdateTimeout);
  }
  
  muteUpdateTimeout = setTimeout(async () => {
    try {
      await apiClient.setMutePercent(mutePercentage.value);
    } catch (error) {
      console.error('Failed to update mute percentage:', error);
      errorMessage.value = `Failed to update volume: ${error.message}`;
    }
  }, 100);
}

// WebSocket live updates
function setupLiveUpdates() {
  apiClient.connectLiveUpdates(
    (data) => {
      // Handle live updates
      if (data.event === 'preset' && data.name) {
        // Update active preset
        presets.value = presets.value.map(p => ({
          ...p,
          isCurrent: p.name === data.name
        }));
      }
      // Add more event handlers as needed
    },
    (error) => {
      console.error('WebSocket error:', error);
    },
    () => {
      console.log('WebSocket disconnected');
    }
  );
}

// Lifecycle
onMounted(() => {
  loadSystemData();
  setupLiveUpdates();

  watch(showNewPresetDialog, async (newValue) => {
    if (newValue) {
      await nextTick(); // Wait for the dialog and input to be rendered
      if (newPresetNameInput.value) {
        newPresetNameInput.value.focus();
      }
    }
  });
});

onUnmounted(() => {
  apiClient.disconnectLiveUpdates();
});
</script>

<style scoped>
@reference '../style.css';
.control-section {
  @apply bg-vybes-dark-element p-6 rounded-lg shadow-lg;
}

.section-title {
  @apply text-xl font-semibold text-vybes-accent mb-4;
}

.preset-button {
  @apply relative px-4 py-2 rounded-lg font-medium transition-all duration-200 flex items-center space-x-2;
}

.preset-active {
  @apply bg-vybes-primary text-white shadow-lg pr-20;
}

.preset-inactive {
  @apply bg-vybes-dark-card text-vybes-text-primary hover:bg-vybes-dark-input border border-vybes-border;
}

.preset-add {
  @apply bg-vybes-accent text-vybes-dark hover:bg-vybes-accent-light border-2 border-dashed border-vybes-accent;
}

.preset-controls {
  @apply absolute right-2 top-1/2 transform -translate-y-1/2 flex space-x-1;
}

.preset-control-btn {
  @apply p-1 hover:bg-white/20 rounded transition-colors;
}

.switch-container {
  @apply flex items-center space-x-3 cursor-pointer w-1/3;
}

.switch-input {
  @apply sr-only;
}

.switch-slider {
  @apply relative inline-block w-12 h-6 bg-gray-600 rounded-full transition-colors duration-200;
}

.switch-input:checked + .switch-slider {
  @apply bg-vybes-primary;
}

.switch-slider::before {
  @apply content-[''] absolute top-0.5 left-0.5 w-5 h-5 bg-white rounded-full transition-transform duration-200;
}

.switch-input:checked + .switch-slider::before {
  @apply transform translate-x-6;
}

.switch-label {
  @apply text-vybes-text-primary font-medium;
}

.slider {
  @apply h-2 bg-vybes-dark-input rounded-lg appearance-none cursor-pointer;
}

.slider::-webkit-slider-thumb {
  @apply appearance-none w-5 h-5 bg-vybes-primary rounded-full cursor-pointer;
}

.slider::-moz-range-thumb {
  @apply w-5 h-5 bg-vybes-primary rounded-full cursor-pointer border-none;
}

.tools-button {
  @apply flex items-center px-6 py-3 bg-vybes-accent text-vybes-dark font-semibold rounded-lg hover:bg-vybes-accent-light transition-colors;
}

.modal-overlay {
  @apply fixed inset-0 bg-black/50 flex items-center justify-center z-50;
}

.modal-content {
  @apply bg-vybes-dark-element p-6 rounded-lg shadow-xl max-w-md w-full mx-4;
}

/* CSS custom properties for theming - add these to your main CSS file */
:root {
  --vybes-primary: #3b82f6;
  --vybes-primary-dark: #2563eb;
  --vybes-accent: #f59e0b;
  --vybes-accent-light: #fbbf24;
  --vybes-dark: #111827;
  --vybes-dark-element: #1f2937;
  --vybes-dark-card: #374151;
  --vybes-dark-input: #4b5563;
  --vybes-border: #6b7280;
  --vybes-text-primary: #f9fafb;
  --vybes-text-secondary: #d1d5db;
}
</style>