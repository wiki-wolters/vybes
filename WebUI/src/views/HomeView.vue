<template>
  <div class="container mx-auto px-0 sm:px-4 py-3">
    <!-- Loading State -->
    <div v-if="isLoading" class="text-center py-10">
      <svg class="animate-spin h-8 w-8 text-vybes-primary mx-auto mb-3" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
        <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
        <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
      </svg>
      <p class="text-vybes-text-secondary">Loading system...</p>
    </div>

    <!-- Error Message -->
    <div v-if="errorMessage" class="bg-red-700 text-red-100 p-4 rounded-none sm:rounded-lg mb-6">
      <p><strong>Error:</strong> {{ errorMessage }}</p>
    </div>

    <!-- Main Content -->
    <div v-if="!isLoading">
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

      <!-- Volume -->
      <CardSection title="Volume">
        <div class="space-y-4">
          <RangeSlider
            :model-value="volume"
            label="Master Volume"
            :min="0"
            :max="100"
            :step="1"
            unit="%"
            @update:modelValue="updateVolume($event)"
          />
        </div>
      </CardSection>

      <!-- Input Source -->
      <CardSection title="Input Source">
        <div class="space-y-4">
          <RangeSlider
            :model-value="inputGainsDB.bluetooth"
            label="Bluetooth"
            :min="-40"
            :max="MAX_DB"
            :step="0.1"
            unit="dB"
            @update:modelValue="updateInputGain('bluetooth', $event)"
          />
          <RangeSlider
            :model-value="inputGainsDB.spdif"
            label="TV"
            :min="-40"
            :max="MAX_DB"
            :step="0.1"
            unit="dB"
            @update:modelValue="updateInputGain('spdif', $event)"
          />
          <RangeSlider
            :model-value="inputGainsDB.usb"
            label="USB"
            :min="-40"
            :max="MAX_DB"
            :step="0.1"
            unit="dB"
            @update:modelValue="updateInputGain('usb', $event)"
          />
          <RangeSlider
            :model-value="inputGainsDB.tone"
            label="Tone"
            :min="-40"
            :max="MAX_DB"
            :step="0.1"
            unit="dB"
            @update:modelValue="updateInputGain('tone', $event)"
          />
          <RangeSlider
            :model-value="inputGainsDB.analog"
            label="Analog"
            :min="-40"
            :max="MAX_DB"
            :step="0.1"
            unit="dB"
            @update:modelValue="updateInputGain('analog', $event)"
          />
        </div>
      </CardSection>

      <!-- Speakers: mute groups derived from the active preset's outputs -->
      <CardSection v-if="muteGroups.length" title="Speakers">
        <div class="flex justify-between gap-3 flex-wrap">
          <div v-for="group in muteGroups" :key="group.id" class="switch-container">
            <ToggleSwitch
              :model-value="group.playing"
              :label="group.label"
              @update:modelValue="toggleMuteGroup(group, $event)"
            />
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
        class="mb-3"
        @keyup.enter="createNewPreset"
      />
      <TemplateSelect v-model="newPresetTemplate" />
    </ModalDialog>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted, watch, nextTick } from 'vue';
import { useRouter } from 'vue-router';
import apiClient from '../api-client.js';
import CardSection from '../components/shared/CardSection.vue';
import InputGroup from '../components/shared/InputGroup.vue';
import RangeSlider from '../components/shared/RangeSlider.vue';
import ModalDialog from '../components/shared/ModalDialog.vue';
import ToggleSwitch from '../components/shared/ToggleSwitch.vue';
import TemplateSelect from '../components/shared/TemplateSelect.vue';

const router = useRouter();

// State
const isLoading = ref(true);
const errorMessage = ref('');
const presets = ref([]);
// Active preset's V1 config (drives the mute groups)
const activePresetName = ref(null);
const activeOutputs = ref([]);
const muteEnabled = ref(false);
const mutePercentage = ref(100);
const inputGainsDB = ref({ bluetooth: -40, spdif: -40, usb: -40, tone: -40, analog: -40 });
const inputGainsLinear = ref({ bluetooth: 0, spdif: 0, usb: 0, tone: 0, analog: 0 });
let muteUpdateTimeout = null;
let inputGainsUpdateTimeout = null;
const showNewPresetDialog = ref(false);
const newPresetName = ref('');
const newPresetTemplate = ref('2.1');
const newPresetNameInput = ref(null);
const volume = ref(50);
let volumeUpdateTimeout = null;

const MIN_DB = -40;
const MAX_DB = 0;

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
      muteEnabled.value = Boolean(status.mute?.muted);
      mutePercentage.value = status.mute?.percent ?? 100;
      if (status.inputGains) {
        inputGainsLinear.value = { ...status.inputGains };
        inputGainsDB.value.bluetooth = linearToDb(status.inputGains.bluetooth);
        inputGainsDB.value.spdif = linearToDb(status.inputGains.spdif);
        inputGainsDB.value.usb = linearToDb(status.inputGains.usb);
        inputGainsDB.value.tone = linearToDb(status.inputGains.tone);
        inputGainsDB.value.analog = linearToDb(status.inputGains.analog);
      }
      volume.value = status.volume ?? 50;
      await loadActivePresetOutputs(status.currentPreset);
    } catch (statusError) {
      console.warn('Could not load system status:', statusError);
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
        inputGainsLinear.value.usb,
        inputGainsLinear.value.tone,
        inputGainsLinear.value.analog
      );
    } catch (error) {
      console.error('Failed to update input gains:', error);
      errorMessage.value = `Failed to update input gains: ${error.message}`;
    }
  }, 250);
}

function updateVolume(newValue) {
  if (volumeUpdateTimeout) {
    clearTimeout(volumeUpdateTimeout);
  }

  volume.value = newValue;

  volumeUpdateTimeout = setTimeout(async () => {
    try {
      await apiClient.setVolume(volume.value);
    } catch (error) {
      console.error('Failed to update volume:', error);
      errorMessage.value = `Failed to update volume: ${error.message}`;
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
    await apiClient.createPreset(newPresetName.value.trim(), newPresetTemplate.value);

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

// ===== Mute groups =====
// The active preset's enabled outputs, grouped by what they play (derived
// from the source mix): left-fed, right-fed, and mono-fed ("Subs"). For a
// 2.1 preset this reproduces the classic Left / Right / Subwoofer toggles.

async function loadActivePresetOutputs(presetName) {
  if (!presetName) {
    activePresetName.value = null;
    activeOutputs.value = [];
    return;
  }
  try {
    const preset = await apiClient.getPreset(presetName);
    activePresetName.value = presetName;
    activeOutputs.value = preset.outputs.map((o, index) => ({ ...o, index }));
  } catch (error) {
    console.error('Failed to load active preset outputs:', error);
    activeOutputs.value = [];
  }
}

const muteGroups = computed(() => {
  const groups = [
    { id: 'left', label: 'Left', outputs: [] },
    { id: 'right', label: 'Right', outputs: [] },
    { id: 'subs', label: 'Subwoofer', outputs: [] },
  ];
  for (const output of activeOutputs.value) {
    if (!output.enabled) continue;
    const { left, right } = output.source;
    if (left > 0 && right > 0) groups[2].outputs.push(output);
    else if (left > 0) groups[0].outputs.push(output);
    else if (right > 0) groups[1].outputs.push(output);
  }
  if (groups[2].outputs.length > 1) groups[2].label = 'Subwoofers';
  return groups
    .filter((g) => g.outputs.length > 0)
    .map((g) => ({ ...g, playing: g.outputs.every((o) => !o.mute) }));
});

async function toggleMuteGroup(group, playing) {
  const mute = !playing;
  for (const output of group.outputs) {
    // Optimistic; websocket outputChanged broadcasts confirm each one
    const local = activeOutputs.value[output.index];
    if (local) local.mute = mute;
    try {
      await apiClient.setOutputMute(activePresetName.value, output.index, mute);
    } catch (error) {
      console.error('Failed to toggle output mute:', error);
      errorMessage.value = `Failed to toggle ${group.label}: ${error.message}`;
      await loadActivePresetOutputs(activePresetName.value);
      return;
    }
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
    muteEnabled.value = !muteEnabled.value;
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
        await apiClient.restore(formData);
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
let unsubscribeLive = null;

function setupLiveUpdates() {
  unsubscribeLive = apiClient.connectLiveUpdates(
    (data) => {
      // Handle live updates
      if (data.messageType === 'activePresetChanged' && data.activePresetName) {
        // Update active preset
        presets.value = presets.value.map(p => ({
          ...p,
          isCurrent: p.name === data.activePresetName
        }));
        loadActivePresetOutputs(data.activePresetName);
      }
      if (data.messageType === 'volumeChanged') {
        volume.value = data.volume;
      }
      // Keep the mute groups in sync with output edits made elsewhere
      if (data.messageType === 'outputChanged' && data.presetName === activePresetName.value) {
        const output = activeOutputs.value[data.output];
        if (output) Object.assign(output, data.changes);
      }
    },
    (error) => {
      console.error('WebSocket error:', error);
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
  if (unsubscribeLive) unsubscribeLive();
});
</script>

<style scoped>
@reference '../style.css';

.preset-button {
  @apply relative px-4 py-2 rounded-lg font-medium transition-all duration-200 flex items-center space-x-2 cursor-pointer;
}

.preset-active {
  @apply bg-vybes-primary text-white shadow-lg pr-14;
}

.preset-inactive {
  @apply bg-vybes-dark-card text-vybes-text-primary hover:bg-vybes-dark-input border border-vybes-border;
}

.preset-add {
  @apply bg-transparent text-vybes-text-secondary border-2 border-dashed border-vybes-border hover:border-vybes-accent hover:text-vybes-accent;
}

.preset-controls {
  @apply absolute right-2 top-1/2 transform -translate-y-1/2 flex space-x-1;
}

.preset-control-btn {
  @apply p-1 hover:bg-white/20 rounded transition-colors cursor-pointer;
}

.switch-container {
  @apply flex items-center space-x-3 cursor-pointer w-1/3;
}
</style>