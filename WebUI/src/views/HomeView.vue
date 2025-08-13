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

      <!-- Input Source -->
      <CardSection title="Input Source">
        <div class="space-y-4">
          <RangeSlider
            :model-value="inputGainsDB.bluetooth"
            label="Bluetooth"
            :min="-40"
            :max="3.5"
            :step="0.1"
            unit="dB"
            @update:modelValue="updateInputGain('bluetooth', $event)"
          />
          <RangeSlider
            :model-value="inputGainsDB.spdif"
            label="TV"
            :min="-40"
            :max="3.5"
            :step="0.1"
            unit="dB"
            @update:modelValue="updateInputGain('spdif', $event)"
          />
          <RangeSlider
            :model-value="inputGainsDB.tone"
            label="Tone"
            :min="-40"
            :max="3.5"
            :step="0.1"
            unit="dB"
            @update:modelValue="updateInputGain('tone', $event)"
          />
        </div>
      </CardSection>

      <!-- Speakers -->
      <CardSection title="Speakers">
        <div class="flex justify-between gap-3">
          <div class="switch-container">
            <ToggleSwitch v-model="speakersEnabled.sub" @change="toggleSpeaker('sub')" label="Subwoofer"/>
          </div>

          <div class="switch-container">
            <ToggleSwitch v-model="speakersEnabled.left" @change="toggleSpeaker('left')" label="Left" />
          </div>

          <div class="switch-container">
            <ToggleSwitch v-model="speakersEnabled.right" @change="toggleSpeaker('right')" label="Right" />
          </div>
        </div>
      </CardSection>

      <CardSection title="Mute">
        <div class="space-y-4">
          <!-- Mute Percentage Slider -->
          <RangeSlider
            :model-value="mutePercentage"
            label="Volume reduction"
            :min="1"
            :max="100"
            unit="%"
            @update:modelValue="updateMutePercentage($event)"
          />
          
          <!-- Mute On/Off Switch -->
          <ToggleSwitch v-model="muteEnabled" @change="toggleMute" label="Mute"/>
        </div>
      </CardSection>

      <CardSection title="Configuration">
        <div class="flex justify-start gap-3">
          <button @click="backupConfiguration" class="btn-secondary">Backup</button>
          <button @click="restoreConfiguration" class="btn-secondary">Restore</button>
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
const speakersEnabled = ref({sub: true, left: true, right: true});
const muteEnabled = ref(false);
const mutePercentage = ref(100);
const inputGainsDB = ref({ bluetooth: -40, spdif: -40, tone: -40 });
const inputGainsLinear = ref({ bluetooth: 0, spdif: 0, tone: 0 });
let muteUpdateTimeout = null;
let inputGainsUpdateTimeout = null;
const calibrationValue = ref(null);
const showNewPresetDialog = ref(false);
const newPresetName = ref('');
const newPresetNameInput = ref(null);

const MIN_DB = -40;
const MAX_DB = 3.5;

function dbToLinear(db) {
  if (db <= MIN_DB) return 0;
  return 10 ** (db / 20);
}

function linearToDb(linear) {
  if (linear <= 0) return MIN_DB;
  const db = 20 * Math.log10(linear);
  return Math.max(MIN_DB, Math.min(MAX_DB, db));
}

// Load initial data
async function loadSystemData() {
  try {
    isLoading.value = true;
    errorMessage.value = '';

    // Load presets
    const presetsData = await apiClient.getPresets();
    presets.value = presetsData || [];

    // Load system status
    try {
      const status = await apiClient.getStatus();
      speakersEnabled.value = {
        sub: status.speakerGains.sub !== 0.0,
        left: status.speakerGains.left !== 0.0,
        right: status.speakerGains.right !== 0.0
      };
      muteEnabled.value = status.muteState === 'on';
      mutePercentage.value = status.mutePercent || 100;
      if (status.inputGains) {
        inputGainsLinear.value = { ...status.inputGains };
        inputGainsDB.value.bluetooth = linearToDb(status.inputGains.bluetooth);
        inputGainsDB.value.spdif = linearToDb(status.inputGains.spdif);
        inputGainsDB.value.tone = linearToDb(status.inputGains.tone);
      }
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

function updateInputGain(source, dbValue) {
  if (inputGainsUpdateTimeout) {
    clearTimeout(inputGainsUpdateTimeout);
  }

  inputGainsDB.value[source] = dbValue;
  inputGainsLinear.value[source] = dbToLinear(dbValue);

  inputGainsUpdateTimeout = setTimeout(async () => {
    try {
      await apiClient.setInputGains(
        inputGainsLinear.value.bluetooth,
        inputGainsLinear.value.spdif,
        inputGainsLinear.value.tone
      );
    } catch (error) {
      console.error('Failed to update input gains:', error);
      errorMessage.value = `Failed to update input gains: ${error.message}`;
    }
  }, 250);
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
async function toggleSpeaker(speaker) {
  try {
    await apiClient.setGlobalSpeakerGain(speaker, speakersEnabled.value[speaker] ? '1.0' : '0.0');
  } catch (error) {
    console.error('Failed to toggle speaker:', error);
    errorMessage.value = `Failed to toggle speaker: ${error.message}`;
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

function updateMutePercentage(newValue) {
  if (muteUpdateTimeout) {
    clearTimeout(muteUpdateTimeout);
  }

  mutePercentage.value = newValue;
  
  muteUpdateTimeout = setTimeout(async () => {
    try {
      await apiClient.setMutePercent(mutePercentage.value);
    } catch (error) {
      console.error('Failed to update mute percentage:', error);
      errorMessage.value = `Failed to update volume: ${error.message}`;
    }
  }, 500);
}

async function backupConfiguration() {
  window.location.href = `${apiClient.baseUrl}/backup`;
}

async function restoreConfiguration() {
  const input = document.createElement('input');
  input.type = 'file';
  input.accept = '.msgpack';
  input.onchange = async (event) => {
    const file = event.target.files[0];
    if (file) {
      const formData = new FormData();
      formData.append('file', file);
      try {
        apiClient.restore(formData);
        alert('Configuration restore initiated. The device will now reboot. The page will reload automatically to reflect the restored state.');
        setTimeout(() => {
          window.location.reload();
        }, 3000);
      } catch (error) {
        // This error is expected because the server reboots.
        // We can ignore it and proceed with the optimistic UI update.
        console.log('Ignoring expected error during restore:', error);
        alert('Configuration restore initiated. The device will now reboot. The page will reload automatically to reflect the restored state.');
        setTimeout(() => {
          window.location.reload();
        }, 3000);
      }
    }
  };
  input.click();
}

// WebSocket live updates
function setupLiveUpdates() {
  apiClient.connectLiveUpdates(
    (data) => {
      // Handle live updates
      if (data.messageType === 'activePresetChanged' && data.activePresetName) {
        // Update active preset
        presets.value = presets.value.map(p => ({
          ...p,
          isCurrent: p.name === data.activePresetName
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