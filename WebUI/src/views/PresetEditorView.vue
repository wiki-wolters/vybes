<template>
  <div class="container mx-auto p-6 bg-vybes-dark-element rounded-lg shadow-xl min-h-[calc(100vh-200px)]">
    
    <div class="hidden md:block mb-6">
      <h1 class="text-3xl font-bold mb-4 text-vybes-light-blue text-center">Preset Editor</h1>
    </div>

    <div v-if="editorMessage" class="mb-6 p-4 rounded-md text-sm text-center transition-all duration-300"
      :class="{
        'bg-green-700 text-green-100': messageType === 'success',
        'bg-red-700 text-red-100': messageType === 'error',
        'bg-blue-700 text-blue-100': messageType === 'info', // [cite: 2]
      }"
      @click="clearMessage"> {{ editorMessage }}
    </div>

    <div v-if="isLoadingPresets" class="text-center py-4 mb-6">
      <svg class="animate-spin h-6 w-6 text-vybes-primary mx-auto" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
        <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
        <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path> </svg>
      <p class="text-vybes-text-secondary text-sm">Loading presets...</p>
    </div>

    <div class="flex flex-col gap-6">
      <div class="w-full">
        <div v-if="isLoadingData" class="text-center py-10">
           <svg class="animate-spin h-8 w-8 text-vybes-primary mx-auto mb-3" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
             <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle> <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path> </svg>
          <p class="text-vybes-text-secondary">Loading preset data...</p>
        </div>
        <div v-else-if="!selectedPresetName || !selectedPresetData" class="text-center py-10"> <p class="text-vybes-text-secondary text-lg">Select a preset to view or edit its details.</p>
          <p class="text-vybes-text-secondary text-sm mt-2">Or, create a new preset to get started.</p>
        </div>
        <div v-else>
          <div class="flex flex-col md:flex-row md:justify-between md:items-center space-y-3 md:space-y-0 mb-4">
            <div class="flex items-center"> <h2 class="text-2xl font-semibold text-vybes-accent mr-2">
                {{ selectedPresetName }}
              </h2>
              <span v-if="selectedPresetData.isCurrent" class="text-sm bg-vybes-accent text-white px-2 py-1 rounded-md">Active</span>
              <div class="ml-2 space-x-1">
                <button @click="openPresetModal('rename', selectedPresetName)" class="btn-icon btn-secondary-icon p-1" title="Rename Preset">✏️</button> <button @click="openPresetModal('copy', selectedPresetName)" class="btn-icon btn-secondary-icon p-1" title="Copy Preset">📋</button> <button @click="handleDeletePreset(selectedPresetName)" class="btn-icon btn-danger-icon p-1" title="Delete Preset">🗑️</button> </div>
            </div>
            <button @click="activatePreset" class="btn-primary w-full md:w-auto" :disabled="selectedPresetData?.isCurrent"> {{ selectedPresetData?.isCurrent ? 'Currently Active' : 'Activate Preset' }} </button>
          </div>

          <CardSection title="Subwoofer Crossover">
            <div>
              <RangeSlider
                v-model="crossoverFreq"
                label="Frequency" :min="40"
                :max="150"
                :step="1"
                unit="Hz"
                :decimals="0"
              /> </div>
          </CardSection>
            
          <CardSection title="Equal Loudness Compensation">
            <div class="flex items-center justify-between">
              <div>
                <span class="text-sm text-vybes-text-secondary">Enable equal loudness compensation</span> <p class="text-sm text-vybes-text-secondary mt-1">Automatically adjusts EQ based on volume level</p>
              </div>
              <label class="relative inline-flex items-center cursor-pointer">
                <input type="checkbox" v-model="equalLoudness" class="sr-only peer">
                <div class="w-11 h-6 bg-vybes-dark-input peer-focus:outline-none peer-focus:ring-2 peer-focus:ring-vybes-primary rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:border-gray-300 after:border after:rounded-full after:h-5 after:w-5 after:transition-all peer-checked:bg-vybes-primary"></div> </label>
            </div>
          </CardSection>

          <CardSection title="Speaker Delays">
            <div class="grid grid-cols-1 md:grid-cols-3 gap-6"> <SpeakerDelayInput title="Left Speaker" v-model="speakerDelays.left" />
              <SpeakerDelayInput title="Right Speaker" v-model="speakerDelays.right" />
              <SpeakerDelayInput title="Subwoofer" v-model="speakerDelays.sub" />
            </div>
          </CardSection>

          <EQSection
            title="Room Correction"
            :eq-sets="roomEQSets"
            :current-spl="currentRoomSPL"
            :eq-points-for-editor="roomEQPointsForEditor"
            @update-current-spl="handleSPLUpdate('room', $event)"
            @select-set="selectEQSet('room', $event)"
            @update-eq-points="handleEQPointsUpdate('room', $event)"
            @delete-set="handleDeleteEQSet('room')"
          />

          <EQSection
            title="Preference Curve" :eq-sets="prefEQSets"
            :current-spl="currentPrefSPL"
            :eq-points-for-editor="prefEQPointsForEditor"
            @update-current-spl="handleSPLUpdate('pref', $event)"
            @select-set="selectEQSet('pref', $event)"
            @update-eq-points="handleEQPointsUpdate('pref', $event)"
            @delete-set="handleDeleteEQSet('pref')"
          />
        </div>
      </div>
    </div>
    
    <ModalDialog
      v-model="showModal"
      :title="modalState.title"
      :confirm-text="modalState.confirmText"
      @confirm="handleModalConfirm"
    >
      <InputGroup v-model="modalState.inputValue" :placeholder="modalState.placeholder" class="w-full mb-4" />
    </ModalDialog>
  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted, watch, inject } from 'vue';
import { throttleAndDebounce } from '../utilities.js'; // [cite: 42] Utility for rate limiting function calls
import ParametricEQ from '../components/ParametricEQ.vue';
import RangeSlider from '../components/shared/RangeSlider.vue';
import InputGroup from '../components/shared/InputGroup.vue';
import CardSection from '../components/shared/CardSection.vue';
import ModalDialog from '../components/shared/ModalDialog.vue';
import SpeakerDelayInput from '../components/shared/SpeakerDelayInput.vue'; // New component for speaker delay inputs
import EQSection from '../components/shared/EQSection.vue'; // New component for EQ sections

// Define props for the component
const props = defineProps({ // [cite: 44]
  name: { // Name of the preset to load, passed via route or prop
    type: String,
    required: false
  }
});
const apiClient = inject('vybesAPI'); // Injected API client for backend communication [cite: 45]
const liveUpdateData = inject('liveUpdateData'); // Injected reactive data for live updates via WebSockets [cite: 45]

// General reactive state
const presets = ref([]); // List of all available presets
const selectedPresetName = ref(null); // Name of the currently selected preset
const selectedPresetData = ref(null); // Holds the full preset data object for the selected preset [cite: 45]
const isLoadingPresets = ref(false); // True when fetching the list of presets [cite: 46]
const isLoadingData = ref(false); // True when fetching data for a selected preset [cite: 46]
const editorMessage = ref(''); // Feedback message displayed to the user [cite: 46]
const messageType = ref('info'); // Type of feedback message: 'success', 'error', 'info' [cite: 47]

// Unified Modal state
const modalState = reactive({
  type: null, // 'create', 'rename', 'copy'
  title: '',
  confirmText: '',
  inputValue: '',
  placeholder: 'Preset Name',
  sourceName: '', // For rename/copy operations
});
const showModal = ref(false); // Controls visibility of the unified modal

// Preset-specific editable properties
const speakerDelays = reactive({ left: 0, right: 0, sub: 0 }); // Speaker delays [cite: 49]
const crossoverFreq = ref(80); // Crossover frequency, Default 80Hz [cite: 50]
// const crossoverSlope = ref('12'); // Default 12dB/oct - Note: Not directly editable in UI, assumed fixed in debouncedSetCrossover [cite: 50]
const equalLoudness = ref(false); // Equal loudness compensation toggle [cite: 51]

// EQ States
const roomEQSets = ref([]); // Array of room correction EQ sets ({ spl: Number, peqSet: Array }) [cite: 58]
const currentRoomSPL = ref(85); // Default SPL value for room correction [cite: 58]
const prefEQSets = ref([]); // Array of preference curve EQ sets ({ spl: Number, peqSet: Array }) [cite: 59]
const currentPrefSPL = ref(85); // Default SPL value for preference curve [cite: 59]

// Helper to get reactive refs for a given EQ type ('room' or 'pref')
const getEQRefs = (type) => type === 'room'
    ? { sets: roomEQSets, currentSPL: currentRoomSPL, dataKey: 'room' }
    : { sets: prefEQSets, currentSPL: currentPrefSPL, dataKey: 'pref' };

// Computed properties for EQ points to be passed to the EQSection/ParametricEQ component
const roomEQPointsForEditor = computed(() => { // [cite: 60]
  const currentSet = roomEQSets.value.find(set => set.spl === currentRoomSPL.value);
  return currentSet ? currentSet.peqSet : [];
});
const prefEQPointsForEditor = computed(() => { // [cite: 61]
  const currentSet = prefEQSets.value.find(set => set.spl === currentPrefSPL.value);
  return currentSet ? currentSet.peqSet : [];
});

// Clears the editor feedback message
const clearMessage = () => { editorMessage.value = ''; messageType.value = 'info'; }; // [cite: 62]

// Generic helper to perform an API call and handle common success/failure patterns
async function performApiCall(apiCall, successCallback, failureMessage) {
  try {
    const response = await apiCall();
    if (successCallback) successCallback(response);
    return true; // Indicate success
  } catch (error) {
    console.error(failureMessage, error);
    editorMessage.value = `${failureMessage}: ${error.message}`;
    messageType.value = 'error';
    return false; // Indicate failure
  }
}

// Create throttled and debounced save functions (limit to 5 calls per second = 200ms, debounce 500ms)
const debouncedSetCrossover = throttleAndDebounce(async () => { // [cite: 51]
  if (!selectedPresetName.value) return;
  const success = await performApiCall(
    () => apiClient.setCrossover(selectedPresetName.value, crossoverFreq.value, '12'), // Assuming '12dB/oct' slope from original code [cite: 51]
    null,
    'Failed to set crossover'
  );
  if (!success && selectedPresetData.value?.crossover) {
    // Reset to previous value on failure
    crossoverFreq.value = selectedPresetData.value.crossover.frequency || 80; // [cite: 51]
  }
}, 500, 200);

const debouncedSetEqualLoudness = throttleAndDebounce(async () => { // [cite: 52]
  if (!selectedPresetName.value) return;
  const success = await performApiCall(
    () => apiClient.setEqualLoudness(selectedPresetName.value, equalLoudness.value),
    null,
    'Failed to toggle equal loudness'
  );
  if (!success) {
    // Reset to previous value on failure
    equalLoudness.value = selectedPresetData.value?.equalLoudness === true; // [cite: 52]
  }
}, 500, 200);

const debouncedSetSpeakerDelay = throttleAndDebounce(async (speaker, delayMs) => { // [cite: 53]
  if (!selectedPresetName.value) return;
  const success = await performApiCall(
    () => apiClient.setSpeakerDelay(selectedPresetName.value, speaker, delayMs),
    null,
    `Failed to set ${speaker} delay`
  );
  if (!success && selectedPresetData.value?.speakerDelays) {
    // Reset to previous value on failure
    speakerDelays[speaker] = selectedPresetData.value.speakerDelays[speaker] || 0; // [cite: 53]
  }
}, 500, 200);

const debouncedSaveEQ = throttleAndDebounce(async (type) => { // Combines debouncedSaveRoomEQ [cite: 54] and debouncedSavePrefEQ [cite: 56]
  if (!selectedPresetName.value) return;
  const { sets, currentSPL, dataKey } = getEQRefs(type);
  const currentSet = sets.value.find(set => set.spl === currentSPL.value);
  if (currentSet) {
    await performApiCall(
      () => apiClient.setEQ(selectedPresetName.value, dataKey, currentSPL.value, currentSet.peqSet),
      null, // No specific success message for debounced saves, UI reflects change
      `Failed to save ${dataKey} EQ`
      // Note: Original code did not have rollback for EQ saves on error, relies on user or re-fetch.
    );
  }
}, 500, 200);

// Fetch all presets from the API
async function fetchPresets() { // [cite: 63]
  isLoadingPresets.value = true; clearMessage();
  const success = await performApiCall(apiClient.getPresets, (data) => {
    presets.value = data || []; // [cite: 65]
    if (presets.value.length > 0 && !selectedPresetName.value) { // [cite: 66]
      // If no preset is selected, select the first one by default, or the active one. [cite: 66]
      const activePreset = presets.value.find(p => p.isCurrent); // [cite: 67]
      selectPreset(activePreset ? activePreset.name : presets.value[0].name); // [cite: 67]
    } else if (presets.value.length === 0) { // [cite: 68]
      selectedPresetName.value = null; selectedPresetData.value = null; // [cite: 68]
    }
  }, 'Failed to load presets'); // [cite: 70]
  if (!success) presets.value = []; // Clear presets on error [cite: 70]
  isLoadingPresets.value = false; // [cite: 71]
}

// Fetch detailed data for a specific preset
async function fetchPresetData(presetName) { // [cite: 72]
  if (!presetName) { selectedPresetData.value = null; isLoadingData.value = false; return; } // [cite: 72]
  isLoadingData.value = true; clearMessage(); // [cite: 73]
  const success = await performApiCall(
    () => apiClient.getPreset(presetName), // [cite: 74]
    (data) => {
      selectedPresetData.value = data; // [cite: 74]
      if (!data) { // [cite: 75]
        editorMessage.value = `Preset '${presetName}' not found or data is empty.`; // [cite: 75]
        messageType.value = 'error'; // [cite: 76]
        selectedPresetName.value = null; // Deselect if data is invalid [cite: 76]
      }
    },
    `Failed to load data for '${presetName}'` // [cite: 77]
  );
  if (!success) selectedPresetData.value = null; // Clear data on error [cite: 77]
  isLoadingData.value = false; // [cite: 79]
}

// Initializes a specific EQ type (room or pref) from loaded preset data
function initEQTypeFromData(data, type) {
  const { sets, currentSPL, dataKey } = getEQRefs(type);
  const eqData = data.eq?.[dataKey]; // e.g. data.eq.room or data.eq.pref
  if (eqData) {
    // Convert the EQ data structure to an array of sets [cite: 86, 89]
    sets.value = Object.entries(eqData)
      .filter(([key]) => key !== 'active') // Filter out non-set properties (like 'active' if it exists) [cite: 86, 89]
      .map(([spl, peqSet]) => ({
        spl: parseInt(spl, 10),
        peqSet: Array.isArray(peqSet) ? peqSet : [] // Ensure peqSet is an array
      }))
      .sort((a, b) => a.spl - b.spl); // Sort by SPL value [cite: 86, 89]
    
    // Set current SPL to the first available set or default [cite: 87, 90]
    currentSPL.value = sets.value.length > 0 ? sets.value[0].spl : 85;
  } else {
    sets.value = []; // No data, empty array
    currentSPL.value = 85; // Default SPL [cite: 88, 91]
  }
}

// Initialize UI elements from loaded preset data
function initializeFromPresetData(data) { // [cite: 80]
  if (!data) { // If no data (e.g., preset deselected), reset fields to defaults
    Object.assign(speakerDelays, {left: 0, right: 0, sub: 0});
    crossoverFreq.value = 80;
    equalLoudness.value = false;
    initEQTypeFromData({}, 'room'); // Reset room EQ
    initEQTypeFromData({}, 'pref'); // Reset pref EQ
    return;
  }
  // Initialize speaker delays [cite: 81]
  Object.assign(speakerDelays, {left: 0, right: 0, sub: 0}, data.speakerDelays);
  
  // Initialize crossover settings [cite: 83]
  crossoverFreq.value = data.crossover?.frequency || 80; // [cite: 83]
  // crossoverSlope.value is assumed fixed or managed elsewhere as per original component structure [cite: 84]
  
  // Initialize equal loudness [cite: 85]
  equalLoudness.value = data.equalLoudness === true;
  
  // Initialize room correction EQ sets [cite: 86]
  initEQTypeFromData(data, 'room');
  // Initialize preference curve EQ sets [cite: 89]
  initEQTypeFromData(data, 'pref');
}

// Selects a preset by name
function selectPreset(presetName) { // [cite: 92]
  if (selectedPresetName.value === presetName && selectedPresetData.value) return; // Avoid refetch if already selected [cite: 92]
  selectedPresetName.value = presetName; // [cite: 93]
  // fetchPresetData will be called by the watcher on selectedPresetName [cite: 94]
}

// Selects an EQ set for a given type and SPL
function selectEQSet(type, spl) {
  getEQRefs(type).currentSPL.value = spl;
  // The EQSection component will display the points for this selected SPL via its computed prop
}

// Handle updates to EQ points from the EQSection component
function handleEQPointsUpdate(type, eqPoints) { // Combines handleRoomEQChange [cite: 94] and handlePrefEQChange [cite: 101]
  const { sets, currentSPL, dataKey } = getEQRefs(type);
  const currentSet = sets.value.find(set => set.spl === currentSPL.value);
  if (currentSet) { // [cite: 96, 103]
    // Update the EQ points in the current set [cite: 96, 103]
    currentSet.peqSet = eqPoints;
    // Update the full preset data structure [cite: 97, 104]
    if (selectedPresetData.value.eq[dataKey]) {
        selectedPresetData.value.eq[dataKey][currentSPL.value] = eqPoints;
    } else { // Should not happen if initialized correctly, but as a safeguard
        selectedPresetData.value.eq[dataKey] = { [currentSPL.value]: eqPoints };
    }
  } else {
    // Create a new EQ set for this SPL if it doesn't exist (e.g., after SPL change to new value) [cite: 98, 105]
    const newSet = { spl: currentSPL.value, peqSet: eqPoints };
    sets.value.push(newSet);
    sets.value.sort((a, b) => a.spl - b.spl); // Keep sorted [cite: 99, 106]
    // Update the full preset data structure [cite: 99, 107]
     if (selectedPresetData.value.eq[dataKey]) {
        selectedPresetData.value.eq[dataKey][currentSPL.value] = eqPoints;
    } else {
         selectedPresetData.value.eq[dataKey] = { [currentSPL.value]: eqPoints };
    }
  }
  // Auto-save with debouncing [cite: 100, 108]
  debouncedSaveEQ(type);
}

// Handle changes to the current SPL value from the EQSection component's input
function handleSPLUpdate(type, newSPL) { // Combines handleRoomSPLChange [cite: 109] and handlePrefSPLChange [cite: 113]
  const { sets, currentSPL } = getEQRefs(type);
  
  // Validate and round the SPL value
  let validatedSPL = Math.round(newSPL); // [cite: 110, 114]
  if (validatedSPL < 0) validatedSPL = 0; // [cite: 109, 113]
  if (validatedSPL > 120) validatedSPL = 120; // [cite: 110, 114]
  currentSPL.value = validatedSPL;

  // If the SPL value doesn't exist in the sets, create a new empty set [cite: 111, 115]
  if (!sets.value.some(set => set.spl === currentSPL.value)) {
    sets.value.push({ spl: currentSPL.value, peqSet: [] });
    sets.value.sort((a, b) => a.spl - b.spl); // [cite: 112, 116]
    // Ensure the new set is also reflected in selectedPresetData for consistency before potential save
    if (selectedPresetData.value?.eq?.[getEQRefs(type).dataKey]) {
        selectedPresetData.value.eq[getEQRefs(type).dataKey][currentSPL.value] = [];
    }
  }
  
  // If there's already data for this SPL (or it was just created), trigger auto-save for the (potentially empty) set [cite: 112, 116]
  // This ensures new SPL values are persisted even if no points are added immediately.
  const currentSetExists = sets.value.find(set => set.spl === currentSPL.value);
  if (currentSetExists) { // [cite: 112, 117]
     debouncedSaveEQ(type);
  }
}

// Delete an EQ set for the current SPL of a given type
async function handleDeleteEQSet(type) { // Combines deleteRoomEQSet [cite: 129] and deletePrefEQSet [cite: 135]
  if (!selectedPresetName.value) return;
  const { sets, currentSPL, dataKey } = getEQRefs(type);
  const splToDelete = currentSPL.value; // SPL to be deleted

  const success = await performApiCall(
    () => apiClient.deleteEQ(selectedPresetName.value, dataKey, splToDelete), // [cite: 130, 136]
    () => {
      // Remove from local state [cite: 130, 136]
      sets.value = sets.value.filter(set => set.spl !== splToDelete);
      // Select another SPL if available, or default [cite: 131, 137]
      currentSPL.value = sets.value.length > 0 ? sets.value[0].spl : 85; // [cite: 132, 138]
      // No success message shown for deletion, as per original behavior [cite: 133, 139]
    },
    `Failed to delete ${dataKey} EQ set` // [cite: 134, 140]
  );
}

// Activate the currently selected preset
async function activatePreset() { // [cite: 144]
  if (!selectedPresetName.value) return;
  await performApiCall(
    () => apiClient.activatePreset(selectedPresetName.value), // [cite: 145]
    () => {
      // Update local state to mark this preset as active [cite: 145]
      presets.value.forEach(p => p.isCurrent = p.name === selectedPresetName.value);
      if (selectedPresetData.value) selectedPresetData.value.isCurrent = true;
      // No success message shown for activation, as per original behavior [cite: 146]
    },
    'Failed to activate preset' // [cite: 147]
  );
}

// Component lifecycle hook
onMounted(async () => { // [cite: 156]
  console.log('PresetEditorView mounted', { props });
  
  // Fetch presets on component mount [cite: 156]
  await fetchPresets();
  
  // If a preset name is provided via route params (props.name), select it [cite: 156]
  if (props.name) {
    console.log('Selecting preset from route params:', props.name); // [cite: 156]
    selectPreset(props.name);
  } else if (presets.value.length === 0) { // If no presets and no name prop
     selectedPresetData.value = null; // Initialize/clear data
  }
  // If presets exist but no prop.name, first preset or active one is selected by fetchPresets()
});

// Watchers for auto-fetching/re-initializing data when selected preset changes
watch(selectedPresetName, async (newValue, oldValue) => { // [cite: 157]
  if (newValue) {
    await fetchPresetData(newValue); // This will fetch and set selectedPresetData
    // initializeFromPresetData will be called by the watcher on selectedPresetData
  } else {
    // If deselected, clear data
    selectedPresetData.value = null; 
    // initializeFromPresetData will also be called by selectedPresetData watcher
  }
});

watch(selectedPresetData, (newData) => {
    initializeFromPresetData(newData); // Centralized place to update UI from new preset data
}, { deep: true }); // deep watch selectedPresetData for internal changes if any part of it could be reactive and changed externally.

// Add watchers for auto-save functionality on individual property changes [cite: 158]
watch(crossoverFreq, () => { if (selectedPresetName.value) debouncedSetCrossover(); }); // [cite: 158]
watch(equalLoudness, () => { if (selectedPresetName.value) debouncedSetEqualLoudness(); }); // [cite: 159]
// Watch speaker delays for changes [cite: 160]
watch(() => speakerDelays.left, (newValue) => { if (selectedPresetName.value) debouncedSetSpeakerDelay('left', newValue); }); // [cite: 160]
watch(() => speakerDelays.right, (newValue) => { if (selectedPresetName.value) debouncedSetSpeakerDelay('right', newValue); }); // [cite: 161]
watch(() => speakerDelays.sub, (newValue) => { if (selectedPresetName.value) debouncedSetSpeakerDelay('sub', newValue); }); // [cite: 162]

// Watch for live updates from WebSocket (basic implementation) [cite: 163]
watch(liveUpdateData, (update) => {
  if (!update || !update.event) return;

  console.log("PresetEditor: Live update received", update); // [cite: 163]
  const presetName = update.presetName;

  if (update.event === 'presetsChanged' || update.event === 'presetListUpdated') { // [cite: 163]
    editorMessage.value = 'Preset list updated via WebSocket. Refreshing...'; // [cite: 163]
    messageType.value = 'info'; // [cite: 163]
    fetchPresets(); // [cite: 163]
  } else if (update.event === 'eqChanged' && presetName === selectedPresetName.value) { // [cite: 163]
     editorMessage.value = `EQ for '${presetName}' updated via WebSocket. Refreshing...`; // [cite: 163]
     messageType.value = 'info'; // [cite: 163]
    fetchPresetData(presetName); // [cite: 163]
  } else if (update.event === 'activePresetChanged' && presetName) { // [cite: 163]
    editorMessage.value = `Active preset changed to '${presetName}' via WebSocket. Refreshing list...`; // [cite: 164]
    messageType.value = 'info'; // [cite: 164]
    fetchPresets(); // Re-fetch to update 'isCurrent' flags [cite: 164]
  } else if (update.event === 'presetDeleted' && presetName === selectedPresetName.value) { // [cite: 164]
    editorMessage.value = `Currently selected preset '${presetName}' was deleted remotely. Refreshing...`; // [cite: 164]
    messageType.value = 'info'; // [cite: 165]
    selectedPresetName.value = null; // Deselect [cite: 165]
    fetchPresets(); // Refresh list [cite: 165]
  }
}, { deep: true });


// --- CRUD Functions for Presets (Modal Driven) ---

// Opens the unified modal with settings for the specified action type
function openPresetModal(type, currentName = '') {
  modalState.type = type;
  modalState.sourceName = currentName; // Used for rename/copy source
  showModal.value = true;

  switch (type) {
    case 'create': // [cite: 166]
      modalState.title = 'Create New Preset'; // [cite: 39]
      modalState.confirmText = 'Create'; // [cite: 39]
      modalState.inputValue = ''; // [cite: 166]
      modalState.placeholder = 'Preset Name'; // [cite: 39]
      break;
    case 'rename': // [cite: 172]
      modalState.title = `Rename Preset '${currentName}'`; // [cite: 40]
      modalState.confirmText = 'Rename'; // [cite: 40]
      modalState.inputValue = currentName; // Pre-fill with current name [cite: 172]
      modalState.placeholder = 'New Preset Name'; // [cite: 40]
      break;
    case 'copy': // [cite: 182]
      modalState.title = `Copy Preset '${currentName}'`; // [cite: 40]
      modalState.confirmText = 'Copy'; // [cite: 41]
      modalState.inputValue = `${currentName} Copy`; // [cite: 182]
      modalState.placeholder = 'New Preset Name'; // [cite: 41]
      break;
  }
}

// Handles the confirmation (submit) from the unified modal
async function handleModalConfirm() {
  const { type, inputValue, sourceName } = modalState;
  let success = false;
  let newSelectedName = null; // To store the name of the preset to be selected after action

  const trimmedValue = inputValue.trim();
  if (!trimmedValue) {
    editorMessage.value = 'Preset name cannot be empty.'; // [cite: 167, 173, 183]
    messageType.value = 'error'; // [cite: 168, 174, 184]
    return;
  }

  isLoadingPresets.value = true; // Indicate activity on preset list [cite: 168, 175, 185]

  if (type === 'create') {
    success = await performApiCall(
      () => apiClient.createPreset(trimmedValue), // [cite: 169]
      () => { editorMessage.value = `Preset '${trimmedValue}' created successfully.`; messageType.value = 'success'; newSelectedName = trimmedValue; }, // [cite: 169]
      'Failed to create preset' // [cite: 171]
    );
  } else if (type === 'rename') {
    if (trimmedValue === sourceName) { // [cite: 174]
      editorMessage.value = 'New name is the same as the old name.'; // [cite: 174]
      messageType.value = 'info'; // [cite: 175]
      isLoadingPresets.value = false; showModal.value = false; return;
    }
    success = await performApiCall(
      () => apiClient.renamePreset(sourceName, trimmedValue), // [cite: 177]
      () => { editorMessage.value = `Preset '${sourceName}' renamed to '${trimmedValue}'.`; messageType.value = 'success'; newSelectedName = trimmedValue; }, // [cite: 177]
      'Failed to rename preset' // [cite: 181]
    );
  } else if (type === 'copy') {
    if (trimmedValue === sourceName) { // [cite: 184]
      editorMessage.value = 'Copy name cannot be the same as the source name.'; // [cite: 184]
      messageType.value = 'error'; // [cite: 185]
      isLoadingPresets.value = false; return;
    }
    success = await performApiCall(
      () => apiClient.copyPreset(sourceName, trimmedValue), // [cite: 186]
      () => { editorMessage.value = `Preset '${sourceName}' copied to '${trimmedValue}'.`; messageType.value = 'success'; newSelectedName = trimmedValue; }, // [cite: 186]
      'Failed to copy preset' // [cite: 188]
    );
  }

  if (success) {
    await fetchPresets(); // Refresh list after successful operation [cite: 169, 178, 187]
    if (newSelectedName) {
        selectPreset(newSelectedName); // Select the new/renamed/copied preset [cite: 170, 179, 187]
    }
  }
  isLoadingPresets.value = false; // [cite: 171, 181, 188]
  if (success || (type === 'rename' && trimmedValue === sourceName)) { // Close modal on success or if rename was a no-op
      showModal.value = false; // [cite: 169, 175, 178, 186]
  }
}

// Handles deletion of a preset (prompted by a direct button, not modal)
async function handleDeletePreset(name) { // [cite: 189]
  // Basic confirm, consider a more robust modal for this in future [cite: 189]
  if (!window.confirm(`Are you sure you want to delete preset '${name}'? This cannot be undone.`)) {
    return; // [cite: 189]
  }
  isLoadingPresets.value = true; // [cite: 190]
  const success = await performApiCall(
    () => apiClient.deletePreset(name), // [cite: 191]
    () => {
      editorMessage.value = `Preset '${name}' deleted successfully.`; // [cite: 191]
      messageType.value = 'success'; // [cite: 191]
      if (selectedPresetName.value === name) { // [cite: 192]
        selectedPresetName.value = null; // Deselect if it was the deleted one [cite: 192]
        selectedPresetData.value = null; // [cite: 193]
      }
    },
    'Failed to delete preset' // [cite: 195]
  );
  if (success) {
      await fetchPresets(); // Refresh list, will select first preset if current one was deleted and list not empty [cite: 194]
  }
  isLoadingPresets.value = false; // [cite: 195]
}

</script>

<style scoped>
@reference "../style.css"; /* Style reference from original file */

/* Basic Modal Styling (from original) */
.modal-backdrop {
  @apply fixed inset-0 bg-black opacity-60 flex items-center justify-center p-4 transition-opacity duration-300 ease-in-out; /* [cite: 196] */
}
.modal-content {
  @apply bg-vybes-dark-card p-6 rounded-lg shadow-xl w-full max-w-md transform transition-all duration-300 ease-in-out scale-95 opacity-0; /* [cite: 197] */
  animation: fadeInScale 0.3s forwards; /* [cite: 197] */
}
@keyframes fadeInScale { /* [cite: 198] */
  to {
    opacity: 1; /* [cite: 198] */
    transform: scale(1); /* [cite: 198] */
  }
}
.modal-input {
  @apply w-full px-4 py-2 bg-vybes-dark-input border border-vybes-dark-border rounded-md shadow-sm focus:ring-vybes-primary focus:border-vybes-primary text-vybes-text-primary placeholder-vybes-text-secondary; /* [cite: 199] */
}

/* Button styles (assuming you have global button styles or extend as needed) (from original) */
.btn-primary {
  @apply bg-vybes-primary hover:bg-vybes-primary-hover text-white font-semibold py-2 px-4 rounded-md shadow disabled:opacity-50 disabled:cursor-not-allowed; /* [cite: 200] */
}
.btn-secondary {
  @apply bg-vybes-accent hover:bg-vybes-accent-hover text-white font-semibold py-2 px-4 rounded-md shadow disabled:opacity-50 disabled:cursor-not-allowed; /* [cite: 201] */
}
.btn-danger {
   @apply bg-red-600 hover:bg-red-700 text-white font-semibold py-2 px-4 rounded-md shadow disabled:opacity-50 disabled:cursor-not-allowed; /* [cite: 202] */
}

.btn-icon {
  @apply p-1.5 rounded-md transition-colors duration-150; /* [cite: 203] */
}
.btn-secondary-icon {
   @apply bg-vybes-dark-element hover:bg-vybes-accent text-vybes-text-secondary hover:text-white; /* [cite: 203] */
}
.btn-danger-icon {
   @apply bg-vybes-dark-element hover:bg-red-600 text-vybes-text-secondary hover:text-white; /* [cite: 204] */
}

/* Custom scrollbar for preset list if it gets long (from original) */
.preset-list-container::-webkit-scrollbar {
  width: 8px; /* [cite: 204] */
}
.preset-list-container::-webkit-scrollbar-track {
  @apply bg-vybes-dark-input; /* [cite: 205] */
  border-radius: 4px;
}
.preset-list-container::-webkit-scrollbar-thumb {
  @apply bg-vybes-primary; /* [cite: 205] */
  border-radius: 4px;
}
.preset-list-container::-webkit-scrollbar-thumb:hover {
  @apply bg-vybes-primary-hover; /* [cite: 205] */
}
</style>