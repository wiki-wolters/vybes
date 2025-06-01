<template>
  <div class="container mx-auto p-6 bg-vybes-dark-element rounded-lg shadow-xl min-h-[calc(100vh-200px)]">
    
    <!-- Desktop Header (visible only on desktop) -->
    <div class="hidden md:block mb-6">
      <h1 class="text-3xl font-bold mb-4 text-vybes-light-blue text-center">Preset Editor</h1>
    </div>

    <!-- General Feedback Message Area -->
    <div v-if="editorMessage" class="mb-6 p-4 rounded-md text-sm text-center transition-all duration-300"
      :class="{
        'bg-green-700 text-green-100': messageType === 'success',
        'bg-red-700 text-red-100': messageType === 'error',
        'bg-blue-700 text-blue-100': messageType === 'info',
      }"
      @click="editorMessage = ''"> <!-- Click to dismiss -->
      {{ editorMessage }}
    </div>

    <!-- Loading indicator for presets -->
    <div v-if="isLoadingPresets" class="text-center py-4 mb-6">
      <svg class="animate-spin h-6 w-6 text-vybes-primary mx-auto" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
        <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
        <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
      </svg>
      <p class="text-vybes-text-secondary text-sm">Loading presets...</p>
    </div>

    <div class="flex flex-col gap-6">
      <!-- Editing Area - Now Full Width -->
      <div class="w-full">
        <div v-if="isLoadingData" class="text-center py-10">
           <svg class="animate-spin h-8 w-8 text-vybes-primary mx-auto mb-3" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
            <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
            <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
          </svg>
          <p class="text-vybes-text-secondary">Loading preset data...</p>
        </div>
        <div v-else-if="!selectedPresetName || !selectedPresetData" class="text-center py-10">
          <p class="text-vybes-text-secondary text-lg">Select a preset to view or edit its details.</p>
          <p class="text-vybes-text-secondary text-sm mt-2">Or, create a new preset to get started.</p>
        </div>
        <div v-else>
          <!-- Preset Name Editing -->
          <div class="flex flex-col md:flex-row md:justify-between md:items-center space-y-3 md:space-y-0 mb-4">
            <div class="flex items-center">
              <h2 class="text-2xl font-semibold text-vybes-accent mr-2">
                {{ selectedPresetName }}
              </h2>
              <span v-if="selectedPresetData.isCurrent" class="text-sm bg-vybes-accent text-white px-2 py-1 rounded-md">Active</span>
              <div class="ml-2 space-x-1">
                <button @click="promptRenamePreset(selectedPresetName)" class="btn-icon btn-secondary-icon p-1" title="Rename Preset">✏️</button>
                <button @click="promptCopyPreset(selectedPresetName)" class="btn-icon btn-secondary-icon p-1" title="Copy Preset">📋</button>
                <button @click="deletePreset(selectedPresetName)" class="btn-icon btn-danger-icon p-1" title="Delete Preset">🗑️</button>
              </div>
            </div>
            <button @click="activatePreset" class="btn-primary w-full md:w-auto" :disabled="selectedPresetData && selectedPresetData.isCurrent">
              {{ selectedPresetData && selectedPresetData.isCurrent ? 'Currently Active' : 'Activate Preset' }}
            </button>
          </div>

          <!-- Crossover Configuration Section -->
          <CardSection title="Subwoofer Crossover">
            <div>
              <RangeSlider
                v-model="crossoverFreq"
                label="Frequency"
                :min="40"
                :max="150"
                :step="1"
                unit="Hz"
                :decimals="0"
              />
            </div>
          </CardSection>
            
          <!-- Equal Loudness Toggle Section -->
          <CardSection title="Equal Loudness Compensation">
            <div class="flex items-center justify-between">
              <div>
                <span class="text-sm text-vybes-text-secondary">Enable equal loudness compensation</span>
                <p class="text-sm text-vybes-text-secondary mt-1">Automatically adjusts EQ based on volume level</p>
              </div>
              <label class="relative inline-flex items-center cursor-pointer">
                <input type="checkbox" v-model="equalLoudness" class="sr-only peer">
                <div class="w-11 h-6 bg-vybes-dark-input peer-focus:outline-none peer-focus:ring-2 peer-focus:ring-vybes-primary rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:border-gray-300 after:border after:rounded-full after:h-5 after:w-5 after:transition-all peer-checked:bg-vybes-primary"></div>
              </label>
            </div>
          </CardSection>
          <!-- Speaker Delays Section -->
          <CardSection title="Speaker Delays">
            <div class="grid grid-cols-1 md:grid-cols-3 gap-6">
              <!-- Left Speaker -->
              <div class="p-4 bg-vybes-dark-element rounded-lg shadow-sm">
                <h4 class="text-md font-medium mb-3 text-vybes-light-blue">Left Speaker</h4>
                <div class="mb-3">
                  <InputGroup
                    v-model="speakerDelays.left"
                    label="Delay (ms)"
                    type="number"
                    :min="0"
                    :max="100"
                    :step="0.1"
                  />
                </div>

              </div>
              
              <!-- Right Speaker -->
              <div class="p-4 bg-vybes-dark-element rounded-lg shadow-sm">
                <h4 class="text-md font-medium mb-3 text-vybes-light-blue">Right Speaker</h4>
                <div class="mb-3">
                  <InputGroup
                    v-model="speakerDelays.right"
                    label="Delay (ms)"
                    type="number"
                    :min="0"
                    :max="100"
                    :step="0.1"
                  />
                </div>

              </div>
              
              <!-- Subwoofer -->
              <div class="p-4 bg-vybes-dark-element rounded-lg shadow-sm">
                <h4 class="text-md font-medium mb-3 text-vybes-light-blue">Subwoofer</h4>
                <div class="mb-3">
                  <InputGroup
                    v-model="speakerDelays.sub"
                    label="Delay (ms)"
                    type="number"
                    :min="0"
                    :max="100"
                    :step="0.1"
                  />
                </div>

              </div>
            </div>
          </CardSection>
          <!-- Room Correction EQ Section -->
          <CardSection title="Room Correction">
            <template #header-actions>
              <div class="flex items-center space-x-2">
                <label class="text-sm text-vybes-text-secondary">SPL:</label>
                <input 
                  type="number" 
                  v-model.number="currentRoomSPL" 
                  min="0" 
                  max="120" 
                  step="1" 
                  class="w-20 px-2 py-1 bg-vybes-dark-input border border-vybes-dark-border rounded-md text-vybes-text-primary text-sm"
                  @change="handleRoomSPLChange"
                >
              </div>
            </template>
            
            <!-- Room Correction EQ Sets List -->
            <div v-if="roomEQSets.length > 0" class="mb-4">
              <div class="text-sm text-vybes-text-secondary mb-2">Available EQ Sets:</div>
              <div class="flex flex-wrap gap-2">
                <button 
                  v-for="set in roomEQSets" 
                  :key="set.spl" 
                  @click="selectRoomEQSet(set.spl)" 
                  :class="['px-3 py-1 rounded-md text-sm', 
                  currentRoomSPL === set.spl ? 'bg-vybes-primary text-white' : 'bg-vybes-dark-input text-vybes-text-secondary hover:bg-vybes-dark-hover']"
                >
                  {{ set.spl }} SPL
                </button>
              </div>
            </div>
            
            <!-- Room Correction EQ Editor -->
            <div class="bg-vybes-dark-card p-4 rounded-lg mb-4">
              <ParametricEQ 
                :eq-points="roomEQPointsForEditor"
                @update:eq-points="handleRoomEQChange"
                class="min-h-[400px] h-auto"
              />
            </div>
            <template #actions>
              <button @click="deleteRoomEQSet" class="btn-danger" v-if="roomEQSets.some(set => set.spl === currentRoomSPL)">Delete This EQ Set</button>
            </template>
          </CardSection>
          <!-- Preference Curve EQ Section -->
          <CardSection title="Preference Curve">
            <template #header-actions>
              <div class="flex items-center space-x-2">
                <label class="text-sm text-vybes-text-secondary">SPL:</label>
                <input 
                  type="number" 
                  v-model.number="currentPrefSPL" 
                  min="0" 
                  max="120" 
                  step="1" 
                  class="w-20 px-2 py-1 bg-vybes-dark-input border border-vybes-dark-border rounded-md text-vybes-text-primary text-sm"
                  @change="handlePrefSPLChange"
                >
              </div>
            </template>
            
            <!-- Preference EQ Sets List -->
            <div v-if="prefEQSets.length > 0" class="mb-4">
              <div class="text-sm text-vybes-text-secondary mb-2">Available EQ Sets:</div>
              <div class="flex flex-wrap gap-2">
                <button 
                  v-for="set in prefEQSets" 
                  :key="set.spl" 
                  @click="selectPrefEQSet(set.spl)" 
                  :class="['px-3 py-1 rounded-md text-sm', 
                  currentPrefSPL === set.spl ? 'bg-vybes-primary text-white' : 'bg-vybes-dark-input text-vybes-text-secondary hover:bg-vybes-dark-hover']"
                >
                  {{ set.spl }} SPL
                </button>
              </div>
            </div>
            
            <!-- Preference Curve EQ Editor -->
            <div class="bg-vybes-dark-card p-4 rounded-lg mb-4">
              <ParametricEQ 
                :eq-points="prefEQPointsForEditor"
                @update:eq-points="handlePrefEQChange"
                class="min-h-[400px] h-auto"
              />
            </div>
            <template #actions>
              <button @click="deletePrefEQSet" class="btn-danger" v-if="prefEQSets.some(set => set.spl === currentPrefSPL)">Delete This EQ Set</button>
            </template>
          </CardSection>
        </div>
      </div>
    </div>
    
    <!-- Create Preset Modal -->
    <ModalDialog
      v-model="showCreateModal"
      title="Create New Preset"
      confirm-text="Create"
      @confirm="createPreset"
    >
      <InputGroup
        v-model="newPresetName"
        placeholder="Preset Name"
        class="w-full mb-4"
      />
    </ModalDialog>
    
    <!-- Rename Preset Modal -->
    <ModalDialog
      v-model="showRenameModal"
      :title="`Rename Preset '${selectedPresetName}'`"
      confirm-text="Rename"
      @confirm="renamePreset"
    >
      <InputGroup
        v-model="renamePresetName"
        placeholder="New Preset Name"
        class="w-full mb-4"
      />
    </ModalDialog>

    <!-- Copy Preset Modal -->
    <ModalDialog
      v-model="showCopyModal"
      :title="`Copy Preset '${copyPresetSourceName}'`"
      confirm-text="Copy"
      @confirm="copyPreset"
    >
      <InputGroup
        v-model="copyPresetNewName"
        placeholder="New Preset Name"
        class="w-full mb-4"
      />
    </ModalDialog>
  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted, watch, inject, defineProps } from 'vue';

import { throttleAndDebounce } from '../utilities.js';
import ParametricEQ from '../components/ParametricEQ.vue';
import RangeSlider from '../components/shared/RangeSlider.vue';
import InputGroup from '../components/shared/InputGroup.vue';
import SelectGroup from '../components/shared/SelectGroup.vue';
import CardSection from '../components/shared/CardSection.vue';
import ModalDialog from '../components/shared/ModalDialog.vue';

// Define props for the component
const props = defineProps({
  name: {
    type: String,
    required: false
  }
});

const apiClient = inject('vybesAPI');
const liveUpdateData = inject('liveUpdateData');

const presets = ref([]);
const selectedPresetName = ref(null);
const selectedPresetData = ref(null); // Holds the full preset data object
const isLoadingPresets = ref(false);
const isLoadingData = ref(false);
const editorMessage = ref('');
const messageType = ref('info'); // 'success', 'error', 'info'

// Modal states
const showCreateModal = ref(false);
const newPresetName = ref('');
const showRenameModal = ref(false);
const renamePresetName = ref('');
const showCopyModal = ref(false);
const copyPresetSourceName = ref('');
const copyPresetNewName = ref('');

// Current active tab/pane
const currentPANE = ref('general');

// Speaker delays
const speakerDelays = reactive({
  left: 0,
  right: 0,
  sub: 0
});

// Crossover settings
const crossoverFreq = ref(80); // Default 80Hz
const crossoverSlope = ref('12'); // Default 12dB/oct

// Equal loudness toggle
const equalLoudness = ref(false);

// Create throttled and debounced save functions (limit to 5 calls per second = 200ms)
const debouncedSetCrossover = throttleAndDebounce(async () => {
  if (!selectedPresetName.value) return;
  try {
    await apiClient.setCrossover(selectedPresetName.value, crossoverFreq.value, crossoverSlope.value);
    editorMessage.value = `Subwoofer crossover frequency set to ${crossoverFreq.value}Hz`;
    messageType.value = 'success';
    setTimeout(() => {
      if (editorMessage.value === `Subwoofer crossover frequency set to ${crossoverFreq.value}Hz`) {
        editorMessage.value = '';
      }
    }, 2000);
  } catch (error) {
    console.error('Failed to set crossover:', error);
    editorMessage.value = `Failed to set crossover: ${error.message}`;
    messageType.value = 'error';
    
    // Reset to previous value
    if (selectedPresetData.value?.crossover) {
      crossoverFreq.value = selectedPresetData.value.crossover.frequency || 80;
    }
  }
}, 500, 200);

const debouncedSetEqualLoudness = throttleAndDebounce(async () => {
  if (!selectedPresetName.value) return;
  try {
    await apiClient.setEqualLoudness(selectedPresetName.value, equalLoudness.value);
    editorMessage.value = `Equal loudness ${equalLoudness.value ? 'enabled' : 'disabled'}`;
    messageType.value = 'success';
    setTimeout(() => {
      if (editorMessage.value === `Equal loudness ${equalLoudness.value ? 'enabled' : 'disabled'}`) {
        editorMessage.value = '';
      }
    }, 2000);
  } catch (error) {
    console.error('Failed to toggle equal loudness:', error);
    editorMessage.value = `Failed to toggle equal loudness: ${error.message}`;
    messageType.value = 'error';
    
    // Reset to previous value
    equalLoudness.value = selectedPresetData.value?.equalLoudness === true;
  }
}, 500, 200);

const debouncedSetSpeakerDelay = throttleAndDebounce(async (speaker, delayMs) => {
  if (!selectedPresetName.value) return;
  try {
    await apiClient.setSpeakerDelay(selectedPresetName.value, speaker, delayMs);
    editorMessage.value = `${speaker.charAt(0).toUpperCase() + speaker.slice(1)} speaker delay set to ${delayMs}ms`;
    messageType.value = 'success';
    setTimeout(() => {
      if (editorMessage.value === `${speaker.charAt(0).toUpperCase() + speaker.slice(1)} speaker delay set to ${delayMs}ms`) {
        editorMessage.value = '';
      }
    }, 2000);
  } catch (error) {
    console.error(`Failed to set ${speaker} delay:`, error);
    editorMessage.value = `Failed to set ${speaker} delay: ${error.message}`;
    messageType.value = 'error';
    
    // Reset to previous value
    if (selectedPresetData.value?.speakerDelays) {
      speakerDelays[speaker] = selectedPresetData.value.speakerDelays[speaker] || 0;
    }
  }
}, 500, 200);

const debouncedSaveRoomEQ = throttleAndDebounce(async () => {
  if (!selectedPresetName.value) return;
  try {
    // Find the current EQ set
    const currentSet = roomEQSets.value.find(set => set.spl === currentRoomSPL.value);
    if (currentSet) {
      await apiClient.setEQ(
        selectedPresetName.value,
        'room',
        currentRoomSPL.value,
        currentSet.peqSet
      );
      editorMessage.value = `Room EQ changes saved`;
      messageType.value = 'success';
      setTimeout(() => {
        if (editorMessage.value === `Room EQ changes saved`) {
          editorMessage.value = '';
        }
      }, 2000);
    }
  } catch (error) {
    console.error('Failed to save room EQ changes:', error);
    editorMessage.value = `Failed to save room EQ: ${error.message}`;
    messageType.value = 'error';
  }
}, 500, 200);

const debouncedSavePrefEQ = throttleAndDebounce(async () => {
  if (!selectedPresetName.value) return;
  try {
    // Find the current EQ set
    const currentSet = prefEQSets.value.find(set => set.spl === currentPrefSPL.value);
    if (currentSet) {
      await apiClient.setEQ(
        selectedPresetName.value,
        'pref',
        currentPrefSPL.value,
        currentSet.peqSet
      );
      editorMessage.value = `Preference EQ changes saved`;
      messageType.value = 'success';
      setTimeout(() => {
        if (editorMessage.value === `Preference EQ changes saved`) {
          editorMessage.value = '';
        }
      }, 2000);
    }
  } catch (error) {
    console.error('Failed to save preference EQ changes:', error);
    editorMessage.value = `Failed to save preference EQ: ${error.message}`;
    messageType.value = 'error';
  }
}, 500, 200);

// Room correction EQ
const roomEQSets = ref([]);
const currentRoomSPL = ref(85); // Default SPL value

// Preference curve EQ
const prefEQSets = ref([]);
const currentPrefSPL = ref(85); // Default SPL value

// Computed properties for EQ points
const roomEQPointsForEditor = computed(() => {
  if (!selectedPresetData.value?.eq?.room) return [];
  
  // Find the EQ set matching the current SPL
  const currentSet = roomEQSets.value.find(set => set.spl === currentRoomSPL.value);
  return currentSet ? currentSet.peqSet : [];
});

const prefEQPointsForEditor = computed(() => {
  if (!selectedPresetData.value?.eq?.pref) return [];
  
  // Find the EQ set matching the current SPL
  const currentSet = prefEQSets.value.find(set => set.spl === currentPrefSPL.value);
  return currentSet ? currentSet.peqSet : [];
});

function clearMessage() {
  editorMessage.value = '';
  messageType.value = 'info';
}

// Fetch all presets from the API
async function fetchPresets() {
  isLoadingPresets.value = true;
  clearMessage();
  try {
    if (!apiClient || typeof apiClient.getPresets !== 'function') {
        throw new Error('API client or getPresets method is not available.');
    }
    const fetchedPresets = await apiClient.getPresets();
    presets.value = fetchedPresets || [];
    if (presets.value.length > 0 && !selectedPresetName.value) {
      // If no preset is selected, select the first one by default, or the active one.
      const activePreset = presets.value.find(p => p.isCurrent);
      selectPreset(activePreset ? activePreset.name : presets.value[0].name);
    } else if (presets.value.length === 0) {
      selectedPresetName.value = null;
      selectedPresetData.value = null;
    }
  } catch (error) {
    console.error('Failed to fetch presets:', error);
    editorMessage.value = `Failed to load presets: ${error.message}`;
    messageType.value = 'error';
    presets.value = []; // Clear presets on error
  } finally {
    isLoadingPresets.value = false;
  }
}

async function fetchPresetData(presetName) {
  if (!presetName) {
    selectedPresetData.value = null;
    isLoadingData.value = false;
    return;
  }
  isLoadingData.value = true;
  clearMessage();
  try {
    if (!apiClient || typeof apiClient.getPreset !== 'function') {
        throw new Error('API client or getPreset method is not available.');
    }
    const data = await apiClient.getPreset(presetName);
    selectedPresetData.value = data;
     if (!data) {
        editorMessage.value = `Preset '${presetName}' not found or data is empty.`;
        messageType.value = 'error';
        selectedPresetName.value = null; // Deselect if data is invalid
    }
  } catch (error) {
    console.error(`Failed to fetch data for preset ${presetName}:`, error);
    editorMessage.value = `Failed to load data for '${presetName}': ${error.message}`;
    messageType.value = 'error';
    selectedPresetData.value = null; // Clear data on error
    // selectedPresetName.value = null; // Potentially deselect if the error is critical
  } finally {
    isLoadingData.value = false;
  }
}

// Initialize UI elements from loaded preset data
function initializeFromPresetData(data) {
  if (!data) return;
  
  // Initialize speaker delays
  if (data.speakerDelays) {
    speakerDelays.left = data.speakerDelays.left || 0;
    speakerDelays.right = data.speakerDelays.right || 0;
    speakerDelays.sub = data.speakerDelays.sub || 0;
  } else {
    speakerDelays.left = 0;
    speakerDelays.right = 0;
    speakerDelays.sub = 0;
  }
  
  // Initialize crossover settings
  if (data.crossover) {
    crossoverFreq.value = data.crossover.frequency || 80;
    crossoverSlope.value = data.crossover.slope || '12';
  } else {
    crossoverFreq.value = 80;
    crossoverSlope.value = '12';
  }
  
  // Initialize equal loudness
  equalLoudness.value = data.equalLoudness === true;
  
  // Initialize room correction EQ sets
  if (data.eq && data.eq.room) {
    // Convert the room EQ data structure to an array of sets
    roomEQSets.value = Object.entries(data.eq.room)
      .filter(([key, value]) => key !== 'active') // Filter out non-set properties
      .map(([spl, peqSet]) => ({
        spl: parseInt(spl, 10),
        peqSet: Array.isArray(peqSet) ? peqSet : []
      }))
      .sort((a, b) => a.spl - b.spl); // Sort by SPL value
    
    // Set current SPL to the first available set or default
    currentRoomSPL.value = roomEQSets.value.length > 0 ? roomEQSets.value[0].spl : 85;
  } else {
    roomEQSets.value = [];
    currentRoomSPL.value = 85;
  }
  
  // Initialize preference curve EQ sets
  if (data.eq && data.eq.pref) {
    // Convert the preference EQ data structure to an array of sets
    prefEQSets.value = Object.entries(data.eq.pref)
      .filter(([key, value]) => key !== 'active') // Filter out non-set properties
      .map(([spl, peqSet]) => ({
        spl: parseInt(spl, 10),
        peqSet: Array.isArray(peqSet) ? peqSet : []
      }))
      .sort((a, b) => a.spl - b.spl); // Sort by SPL value
    
    // Set current SPL to the first available set or default
    currentPrefSPL.value = prefEQSets.value.length > 0 ? prefEQSets.value[0].spl : 85;
  } else {
    prefEQSets.value = [];
    currentPrefSPL.value = 85;
  }
}

function selectPreset(presetName) {
  if (selectedPresetName.value === presetName && selectedPresetData.value) return; // Avoid refetch if already selected
  selectedPresetName.value = presetName;
  // fetchPresetData will be called by the watcher on selectedPresetName
}

// Handle room correction EQ changes
function handleRoomEQChange(eqPoints) {
  if (!selectedPresetData.value?.eq?.room) return;
  
  // Find the current EQ set
  const currentSet = roomEQSets.value.find(set => set.spl === currentRoomSPL.value);
  
  if (currentSet) {
    // Update the EQ points in the current set
    currentSet.peqSet = eqPoints;
    
    // Update the full preset data structure
    if (!selectedPresetData.value.eq.room[currentRoomSPL.value]) {
      selectedPresetData.value.eq.room[currentRoomSPL.value] = [];
    }
    selectedPresetData.value.eq.room[currentRoomSPL.value] = eqPoints;
  } else {
    // Create a new EQ set for this SPL
    const newSet = { spl: currentRoomSPL.value, peqSet: eqPoints };
    roomEQSets.value.push(newSet);
    roomEQSets.value.sort((a, b) => a.spl - b.spl);
    
    // Update the full preset data structure
    selectedPresetData.value.eq.room[currentRoomSPL.value] = eqPoints;
  }
  
  // Auto-save with debouncing
  debouncedSaveRoomEQ();
}

// Handle preference curve EQ changes
function handlePrefEQChange(eqPoints) {
  if (!selectedPresetData.value?.eq?.pref) return;
  
  // Find the current preference EQ set
  const currentSet = prefEQSets.value.find(set => set.spl === currentPrefSPL.value);
  
  if (currentSet) {
    // Update the existing set
    currentSet.peqSet = eqPoints;
    
    // Update the full preset data structure
    if (!selectedPresetData.value.eq.pref[currentPrefSPL.value]) {
      selectedPresetData.value.eq.pref[currentPrefSPL.value] = [];
    }
    selectedPresetData.value.eq.pref[currentPrefSPL.value] = eqPoints;
  } else {
    // Create a new set if it doesn't exist
    prefEQSets.value.push({
      spl: currentPrefSPL.value,
      peqSet: eqPoints
    });
    // Sort the sets by SPL
    prefEQSets.value.sort((a, b) => a.spl - b.spl);
    
    // Update the full preset data structure
    selectedPresetData.value.eq.pref[currentPrefSPL.value] = eqPoints;
  }
  
  // Auto-save with debouncing
  debouncedSavePrefEQ();
}

// Handle room SPL change
function handleRoomSPLChange() {
  // Validate the SPL value
  if (currentRoomSPL.value < 0) currentRoomSPL.value = 0;
  if (currentRoomSPL.value > 120) currentRoomSPL.value = 120;
  
  // Round to nearest integer
  currentRoomSPL.value = Math.round(currentRoomSPL.value);
  
  // If the SPL value doesn't exist in the sets, create a new empty set
  if (!roomEQSets.value.some(set => set.spl === currentRoomSPL.value)) {
    roomEQSets.value.push({
      spl: currentRoomSPL.value,
      peqSet: []
    });
    roomEQSets.value.sort((a, b) => a.spl - b.spl);
  }
  
  // If there's already data for this SPL, trigger auto-save
  const currentSet = roomEQSets.value.find(set => set.spl === currentRoomSPL.value);
  if (currentSet) {
    debouncedSaveRoomEQ();
  }
}

// Handle preference SPL change
function handlePrefSPLChange() {
  // Validate the SPL value
  if (currentPrefSPL.value < 0) currentPrefSPL.value = 0;
  if (currentPrefSPL.value > 120) currentPrefSPL.value = 120;
  
  // Round to nearest integer
  currentPrefSPL.value = Math.round(currentPrefSPL.value);
  
  // If the SPL value doesn't exist in the sets, create a new empty set
  if (!prefEQSets.value.some(set => set.spl === currentPrefSPL.value)) {
    prefEQSets.value.push({
      spl: currentPrefSPL.value,
      peqSet: []
    });
    prefEQSets.value.sort((a, b) => a.spl - b.spl);
  }
  
  // If there's already data for this SPL, trigger auto-save
  const currentSet = prefEQSets.value.find(set => set.spl === currentPrefSPL.value);
  if (currentSet) {
    debouncedSavePrefEQ();
  }
}

// Save room correction EQ changes
async function saveRoomEQChanges() {
  if (!selectedPresetName.value) return;
  
  try {
    const currentSet = roomEQSets.value.find(set => set.spl === currentRoomSPL.value);
    if (!currentSet) {
      throw new Error('No EQ set found for the current SPL value');
    }
    
    await apiClient.setEQ(selectedPresetName.value, 'room', currentRoomSPL.value, currentSet.peqSet);
    editorMessage.value = 'Room correction EQ saved successfully';
    messageType.value = 'success';
  } catch (error) {
    console.error('Failed to save room correction EQ:', error);
    editorMessage.value = `Failed to save room correction EQ: ${error.message}`;
    messageType.value = 'error';
  }
}

// Save preference curve EQ changes
async function savePrefEQChanges() {
  if (!selectedPresetName.value) return;
  
  try {
    const currentSet = prefEQSets.value.find(set => set.spl === currentPrefSPL.value);
    if (!currentSet) {
      throw new Error('No EQ set found for the current SPL value');
    }
    
    await apiClient.setEQ(selectedPresetName.value, 'pref', currentPrefSPL.value, currentSet.peqSet);
    editorMessage.value = 'Preference curve EQ saved successfully';
    messageType.value = 'success';
  } catch (error) {
    console.error('Failed to save preference curve EQ:', error);
    editorMessage.value = `Failed to save preference curve EQ: ${error.message}`;
    messageType.value = 'error';
  }
}

// Delete room correction EQ set
async function deleteRoomEQSet() {
  if (!selectedPresetName.value) return;
  
  try {
    await apiClient.deleteEQ(selectedPresetName.value, 'room', currentRoomSPL.value);
    
    // Remove from local state
    roomEQSets.value = roomEQSets.value.filter(set => set.spl !== currentRoomSPL.value);
    
    // Select another SPL if available
    if (roomEQSets.value.length > 0) {
      currentRoomSPL.value = roomEQSets.value[0].spl;
    } else {
      currentRoomSPL.value = 85; // Default
    }
    
    editorMessage.value = 'Room correction EQ set deleted successfully';
    messageType.value = 'success';
  } catch (error) {
    console.error('Failed to delete room correction EQ set:', error);
    editorMessage.value = `Failed to delete room correction EQ set: ${error.message}`;
    messageType.value = 'error';
  }
}

// Delete preference curve EQ set
async function deletePrefEQSet() {
  if (!selectedPresetName.value) return;
  
  try {
    await apiClient.deleteEQ(selectedPresetName.value, 'pref', currentPrefSPL.value);
    
    // Remove from local state
    prefEQSets.value = prefEQSets.value.filter(set => set.spl !== currentPrefSPL.value);
    
    // Select another SPL if available
    if (prefEQSets.value.length > 0) {
      currentPrefSPL.value = prefEQSets.value[0].spl;
    } else {
      currentPrefSPL.value = 85; // Default
    }
    
    editorMessage.value = 'Preference curve EQ set deleted successfully';
    messageType.value = 'success';
  } catch (error) {
    console.error('Failed to delete preference curve EQ set:', error);
    editorMessage.value = `Failed to delete preference curve EQ set: ${error.message}`;
    messageType.value = 'error';
  }
}

// Speaker delay handlers
function setSpeakerDelay(speaker, delayMs) {
  if (!selectedPresetName.value) return;
  
  // Update local state
  speakerDelays[speaker] = delayMs;
  
  // Auto-save with debouncing
  debouncedSetSpeakerDelay(speaker, delayMs);
}


// Crossover settings handlers
function setCrossover() {
  if (!selectedPresetName.value) return;
  
  // Auto-save with debouncing
  debouncedSetCrossover();
}

// Equal loudness handler
function toggleEqualLoudness() {
  if (!selectedPresetName.value) return;
  
  // Auto-save with debouncing
  debouncedSetEqualLoudness();
}

// Activate preset
async function activatePreset() {
  if (!selectedPresetName.value) return;
  
  try {
    await apiClient.activatePreset(selectedPresetName.value);
    
    // Update local state to mark this preset as active
    presets.value.forEach(preset => {
      preset.isCurrent = preset.name === selectedPresetName.value;
    });
    
    editorMessage.value = `Preset '${selectedPresetName.value}' activated`;
    messageType.value = 'success';
  } catch (error) {
    console.error('Failed to activate preset:', error);
    editorMessage.value = `Failed to activate preset: ${error.message}`;
    messageType.value = 'error';
  }
}

async function saveEQChanges() {
  if (!selectedPresetName.value || !selectedPresetData.value?.eq?.room?.peqSet) {
    editorMessage.value = 'No preset selected or no EQ data to save.';
    messageType.value = 'error';
    return;
  }
  isLoadingData.value = true; // Indicate activity
  try {
    if (!apiClient || typeof apiClient.setEQ !== 'function') {
        throw new Error('API client or setEQ method is not available.');
    }
    // Assuming 'room' is the type. This might need to be dynamic if other EQ types exist.
    await apiClient.setEQ(
      selectedPresetName.value,
      'room', // This should ideally come from the preset data or a selector
      selectedPresetData.value.eq.room.spl,
      selectedPresetData.value.eq.room.peqSet
    );
    editorMessage.value = 'EQ changes saved successfully!';
    messageType.value = 'success';
    // Optionally re-fetch data to confirm, or trust the update
    // await fetchPresetData(selectedPresetName.value); 
  } catch (error) {
    console.error('Failed to save EQ changes:', error);
    editorMessage.value = `Failed to save EQ: ${error.message}`;
    messageType.value = 'error';
  } finally {
    isLoadingData.value = false;
  }
}


onMounted(async () => {
  console.log('PresetEditorView mounted', { props });
  
  // Fetch presets on component mount
  await fetchPresets();
  
  // If a preset name is provided via route params, select it
  if (props.name) {
    console.log('Selecting preset from route params:', props.name);
    selectPreset(props.name);
  } else if (!newName) {
    selectedPresetData.value = null; // Initialize data when preset is selected
  }
});

watch(selectedPresetName, async (newValue) => {
  if (newValue) {
    await fetchPresetData(newValue);
    initializeFromPresetData(selectedPresetData.value);
  }
});

// Add watchers for auto-save functionality
watch(crossoverFreq, () => {
  if (selectedPresetName.value) {
    debouncedSetCrossover();
  }
});

watch(equalLoudness, () => {
  if (selectedPresetName.value) {
    debouncedSetEqualLoudness();
  }
});

// Watch speaker delays for changes
watch(() => speakerDelays.left, (newValue) => {
  if (selectedPresetName.value) {
    debouncedSetSpeakerDelay('left', newValue);
  }
});

watch(() => speakerDelays.right, (newValue) => {
  if (selectedPresetName.value) {
    debouncedSetSpeakerDelay('right', newValue);
  }
});

watch(() => speakerDelays.sub, (newValue) => {
  if (selectedPresetName.value) {
    debouncedSetSpeakerDelay('sub', newValue);
  }
});

// Watch for live updates (basic implementation)
watch(liveUpdateData, (update) => {
  if (!update || !update.event) return;

  console.log("PresetEditor: Live update received", update);

  if (update.event === 'presetsChanged' || update.event === 'presetListUpdated') {
    editorMessage.value = 'Preset list updated via WebSocket. Refreshing...';
    messageType.value = 'info';
    fetchPresets();
  } else if (update.event === 'eqChanged' && update.presetName === selectedPresetName.value) {
     editorMessage.value = `EQ for '${update.presetName}' updated via WebSocket. Refreshing...`;
     messageType.value = 'info';
    fetchPresetData(selectedPresetName.value);
  } else if (update.event === 'activePresetChanged' && update.presetName) {
    editorMessage.value = `Active preset changed to '${update.presetName}' via WebSocket. Refreshing list...`;
    messageType.value = 'info';
    fetchPresets(); // Re-fetch to update 'isCurrent' flags
  } else if (update.event === 'presetDeleted' && update.presetName === selectedPresetName.value) {
    editorMessage.value = `Currently selected preset '${update.presetName}' was deleted remotely. Refreshing...`;
    messageType.value = 'info';
    selectedPresetName.value = null; // Deselect
    fetchPresets(); // Refresh list
  }
}, { deep: true });


// --- CRUD Functions ---
function promptCreatePreset() {
  newPresetName.value = '';
  showCreateModal.value = true;
}

async function createPreset() {
  if (!newPresetName.value.trim()) {
    editorMessage.value = 'Preset name cannot be empty.';
    messageType.value = 'error';
    return;
  }
  isLoadingPresets.value = true; // Indicate activity on preset list
  try {
    if (!apiClient || typeof apiClient.createPreset !== 'function') {
        throw new Error('API client or createPreset method is not available.');
    }
    await apiClient.createPreset(newPresetName.value.trim());
    editorMessage.value = `Preset '${newPresetName.value.trim()}' created successfully.`;
    messageType.value = 'success';
    showCreateModal.value = false;
    await fetchPresets(); // Refresh list
    selectPreset(newPresetName.value.trim()); // Select the new preset
  } catch (error) {
    console.error('Failed to create preset:', error);
    editorMessage.value = `Failed to create preset: ${error.message}`;
    messageType.value = 'error';
  } finally {
    isLoadingPresets.value = false;
  }
}

function promptRenamePreset(name) {
  selectedPresetName.value = name; // Ensure this is the preset we intend to rename
  renamePresetName.value = name;   // Pre-fill with current name
  showRenameModal.value = true;
}

async function renamePreset() {
  if (!renamePresetName.value.trim()) {
    editorMessage.value = 'New preset name cannot be empty.';
    messageType.value = 'error';
    return;
  }
  if (renamePresetName.value.trim() === selectedPresetName.value) {
    editorMessage.value = 'New name is the same as the old name.';
    messageType.value = 'info';
    showRenameModal.value = false;
    return;
  }
  isLoadingPresets.value = true;
  try {
    if (!apiClient || typeof apiClient.renamePreset !== 'function') {
        throw new Error('API client or renamePreset method is not available.');
    }
    await apiClient.renamePreset(selectedPresetName.value, renamePresetName.value.trim());
    editorMessage.value = `Preset '${selectedPresetName.value}' renamed to '${renamePresetName.value.trim()}'.`;
    messageType.value = 'success';
    const oldSelectedName = selectedPresetName.value;
    const newSelectedName = renamePresetName.value.trim();
    showRenameModal.value = false;
    await fetchPresets(); // Refresh list
    // If the currently selected preset was renamed, update selectedPresetName to the new name
    if (oldSelectedName === selectedPresetName.value || !presets.value.find(p => p.name === oldSelectedName)) {
        selectPreset(newSelectedName);
    }
  } catch (error) {
    console.error('Failed to rename preset:', error);
    editorMessage.value = `Failed to rename preset: ${error.message}`;
    messageType.value = 'error';
  } finally {
    isLoadingPresets.value = false;
  }
}

function promptCopyPreset(name) {
  copyPresetSourceName.value = name;
  copyPresetNewName.value = `${name} Copy`;
  showCopyModal.value = true;
}

async function copyPreset() {
  if (!copyPresetNewName.value.trim()) {
    editorMessage.value = 'New preset name for copy cannot be empty.';
    messageType.value = 'error';
    return;
  }
  if (copyPresetNewName.value.trim() === copyPresetSourceName.value) {
    editorMessage.value = 'Copy name cannot be the same as the source name.';
    messageType.value = 'error';
    return;
  }
  isLoadingPresets.value = true;
  try {
    if (!apiClient || typeof apiClient.copyPreset !== 'function') {
        throw new Error('API client or copyPreset method is not available.');
    }
    await apiClient.copyPreset(copyPresetSourceName.value, copyPresetNewName.value.trim());
    editorMessage.value = `Preset '${copyPresetSourceName.value}' copied to '${copyPresetNewName.value.trim()}'.`;
    messageType.value = 'success';
    showCopyModal.value = false;
    await fetchPresets(); // Refresh list
    selectPreset(copyPresetNewName.value.trim()); // Select the new copy
  } catch (error) {
    console.error('Failed to copy preset:', error);
    editorMessage.value = `Failed to copy preset: ${error.message}`;
    messageType.value = 'error';
  } finally {
    isLoadingPresets.value = false;
  }
}

async function deletePreset(name) {
  // Basic confirm, consider a more robust modal for this
  if (!window.confirm(`Are you sure you want to delete preset '${name}'? This cannot be undone.`)) {
    return;
  }
  isLoadingPresets.value = true;
  try {
    if (!apiClient || typeof apiClient.deletePreset !== 'function') {
        throw new Error('API client or deletePreset method is not available.');
    }
    await apiClient.deletePreset(name);
    editorMessage.value = `Preset '${name}' deleted successfully.`;
    messageType.value = 'success';
    if (selectedPresetName.value === name) {
      selectedPresetName.value = null; // Deselect if it was the deleted one
      selectedPresetData.value = null;
    }
    await fetchPresets(); // Refresh list, will select first preset if current one was deleted
  } catch (error) {
    console.error('Failed to delete preset:', error);
    editorMessage.value = `Failed to delete preset: ${error.message}`;
    messageType.value = 'error';
  } finally {
    isLoadingPresets.value = false;
  }
}

</script>

<style scoped>
@reference "../style.css";

/* Basic Modal Styling */
.modal-backdrop {
  @apply fixed inset-0 bg-black opacity-60 flex items-center justify-center p-4 transition-opacity duration-300 ease-in-out;
}
.modal-content {
  @apply bg-vybes-dark-card p-6 rounded-lg shadow-xl w-full max-w-md transform transition-all duration-300 ease-in-out scale-95 opacity-0;
  animation: fadeInScale 0.3s forwards;
}
@keyframes fadeInScale {
  to {
    opacity: 1;
    transform: scale(1);
  }
}
.modal-input {
  @apply w-full px-4 py-2 bg-vybes-dark-input border border-vybes-dark-border rounded-md shadow-sm focus:ring-vybes-primary focus:border-vybes-primary text-vybes-text-primary placeholder-vybes-text-secondary;
}

/* Button styles (assuming you have global button styles or extend as needed) */
.btn-primary {
  @apply bg-vybes-primary hover:bg-vybes-primary-hover text-white font-semibold py-2 px-4 rounded-md shadow disabled:opacity-50 disabled:cursor-not-allowed;
}
.btn-secondary {
  @apply bg-vybes-accent hover:bg-vybes-accent-hover text-white font-semibold py-2 px-4 rounded-md shadow disabled:opacity-50 disabled:cursor-not-allowed;
}
.btn-danger {
   @apply bg-red-600 hover:bg-red-700 text-white font-semibold py-2 px-4 rounded-md shadow disabled:opacity-50 disabled:cursor-not-allowed;
}

.btn-icon {
  @apply p-1.5 rounded-md transition-colors duration-150;
}
.btn-secondary-icon {
   @apply bg-vybes-dark-element hover:bg-vybes-accent text-vybes-text-secondary hover:text-white;
}
.btn-danger-icon {
   @apply bg-vybes-dark-element hover:bg-red-600 text-vybes-text-secondary hover:text-white;
}

/* Custom scrollbar for preset list if it gets long */
.preset-list-container::-webkit-scrollbar {
  width: 8px;
}
.preset-list-container::-webkit-scrollbar-track {
  @apply bg-vybes-dark-input;
  border-radius: 4px;
}
.preset-list-container::-webkit-scrollbar-thumb {
  @apply bg-vybes-primary;
  border-radius: 4px;
}
.preset-list-container::-webkit-scrollbar-thumb:hover {
  @apply bg-vybes-primary-hover;
}
</style>
