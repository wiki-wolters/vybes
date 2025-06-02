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

    <div class="flex flex-col gap-6">
      <div class="w-full">
        <div v-if="isLoadingData" class="text-center py-10">
           <svg class="animate-spin h-8 w-8 text-vybes-primary mx-auto mb-3" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
             <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle> <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path> </svg>
          <p class="text-vybes-text-secondary">Loading...</p>
        </div>
        <div v-else>
          <div class="flex flex-col md:flex-row md:justify-between md:items-center space-y-3 md:space-y-0 mb-4">
            <div class="flex items-center"> <h2 class="text-2xl font-semibold text-vybes-accent mr-2">
                {{ selectedPresetName }}
              </h2>
              <span v-if="selectedPresetData?.isCurrent" class="text-sm bg-vybes-accent text-white px-2 py-1 rounded-md">Active</span>
              <div class="ml-2 space-x-1">
                <button @click="openPresetModal('rename', selectedPresetName)" class="btn-icon btn-secondary-icon p-1" title="Rename Preset">✏️</button> <button @click="openPresetModal('copy', selectedPresetName)" class="btn-icon btn-secondary-icon p-1" title="Copy Preset">📋</button> <button @click="handleDeletePreset(selectedPresetName)" class="btn-icon btn-danger-icon p-1" title="Delete Preset">🗑️</button> </div>
            </div>
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
            @update-eq-points="handleEQPointsUpdate('room', $event)"
            @delete-set="handleDeleteEQSet('room')"
            @create-new-set="handleCreateNewSet('room', $event)"
          />

          <EQSection
            title="Preference Curve" 
            :eq-sets="prefEQSets"
            @update-eq-points="handleEQPointsUpdate('pref', $event)"
            @delete-set="handleDeleteEQSet('pref')"
            @create-new-set="handleCreateNewSet('pref', $event)"
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
import { useRouter } from 'vue-router';
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
const router = useRouter();

// General reactive state
const presets = ref([]); // List of all available presets
const selectedPresetName = ref(null); // Name of the currently selected preset
const selectedPresetData = ref(null); // Holds the full preset data object for the selected preset [cite: 45]
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
    ? { sets: roomEQSets, currentSPL: currentRoomSPL, type }
    : { sets: prefEQSets, currentSPL: currentPrefSPL, type };

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
  const { sets, currentSPL } = getEQRefs(type);
  const currentSet = sets.value.find(set => set.spl === currentSPL.value);
  if (currentSet) {
    await performApiCall(
      () => apiClient.setEQ(selectedPresetName.value, type, currentSPL.value, currentSet.peqSet),
      null, // No specific success message for debounced saves, UI reflects change
      `Failed to save ${type} EQ`
      // Note: Original code did not have rollback for EQ saves on error, relies on user or re-fetch.
    );
  }
}, 500, 200);

// Handles creating a new EQ set for a given type (room/pref) and SPL
async function handleCreateNewSet(type, newSPL) {
  if (!selectedPresetName.value) {
    editorMessage.value = 'Please select a preset before adding an EQ set.';
    messageType.value = 'error';
    return;
  }
  const { sets, currentSPL } = getEQRefs(type);

  if (sets.value.some(set => set.spl === newSPL)) {
    editorMessage.value = `An EQ set for SPL ${newSPL} already exists in ${type === 'room' ? 'Room Correction' : 'Preference Curve'}.`;
    messageType.value = 'error';
    return;
  }

  const defaultPEQPoints = [
    { freq: 100, gain: 0, q: 1.0 },
    { freq: 1000, gain: 0, q: 1.0 },
    { freq: 10000, gain: 0, q: 1.0 }
  ];
  const newPeqSetPayload = (newSPL === 0) ? defaultPEQPoints : [];

  editorMessage.value = ''; // Clear previous messages
  const success = await performApiCall(
    () => apiClient.setEQ(selectedPresetName.value, type, newSPL, newPeqSetPayload),
    () => {
      sets.value.push({ spl: newSPL, peqSet: [...newPeqSetPayload] }); // Add to local state
      sets.value.sort((a, b) => a.spl - b.spl); // Keep sets sorted by SPL
      currentSPL.value = newSPL; // Select the newly created set
      editorMessage.value = `New EQ set at ${newSPL} SPL created for ${type === 'room' ? 'Room Correction' : 'Preference Curve'}.`;
      messageType.value = 'success';
    },
    `Failed to create new EQ set for ${type === 'room' ? 'Room Correction' : 'Preference Curve'} at ${newSPL} SPL`
  );
}

// Fetch detailed data for a specific preset
async function fetchPresetData(presetName, isNewOrCopy = false) { // [cite: 72]
  if (!presetName) { selectedPresetData.value = null; isLoadingData.value = false; return; } // [cite: 72]
  isLoadingData.value = true; clearMessage(); // [cite: 73]
  const success = await performApiCall(
    () => apiClient.getPreset(presetName), // [cite: 74]
    (data) => {
      selectedPresetData.value = data;
    },
    `Failed to load data for '${presetName}'` // [cite: 77]
  );
  if (!success) selectedPresetData.value = null; // Clear data on error [cite: 77]
  isLoadingData.value = false; // [cite: 79]
}

// Selects a preset by name
async function selectPreset(presetName, isNewOrCopy = false) { // [cite: 92]
  // Allow re-selection if isNewOrCopy is true, to ensure re-initialization logic runs for new/copied presets.
  if (selectedPresetName.value === presetName && selectedPresetData.value && !isNewOrCopy) return;
  selectedPresetName.value = presetName;
  await fetchPresetData(presetName, isNewOrCopy); 
}

// Component lifecycle hook
onMounted(async () => { // [cite: 156]
  console.log('PresetEditorView mounted', { props });

  await selectPreset(props.name);
});

// Handles the confirmation (submit) from the unified modal
async function handleModalConfirm() {
  const { type, inputValue, sourceName } = modalState;
  editorMessage.value = ''; // Clear message before operation
  let success = false;
  let newSelectedName = null; // To store the name of the preset to be selected after action

  const trimmedValue = inputValue.trim();
  if (!trimmedValue) {
    editorMessage.value = 'Preset name cannot be empty.'; // [cite: 167, 173, 183]
    messageType.value = 'error'; // [cite: 168, 174, 184]
    return;
  }

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
    }
    success = await performApiCall(
      () => apiClient.copyPreset(sourceName, trimmedValue), // [cite: 186]
      () => { editorMessage.value = `Preset '${sourceName}' copied to '${trimmedValue}'.`; messageType.value = 'success'; newSelectedName = trimmedValue; }, // [cite: 186]
      'Failed to copy preset' // [cite: 188]
    );
  } else if (type === 'delete') {
    success = await performApiCall(
      () => apiClient.deletePreset(sourceName),
      () => {
        editorMessage.value = `Preset '${sourceName}' deleted successfully.`;
        messageType.value = 'success';
      },
      'Failed to delete preset'
    );
    // If successful, the redirect will happen, so further processing in this function might be skipped
    // or happen just before navigation. If deletion fails, normal error handling will occur.
    // If deletion is successful and we redirect, we might not need to fall through to the generic 'if (success)' block below
    // for preset list refreshing if the page context is about to change entirely.
    // However, if the redirect is conditional or might not happen, ensure 'success' is handled appropriately.
    if (success) {
      // Explicitly stop further processing in this function if redirecting
      // Or ensure that operations like showModal.value = false still execute if desired before redirect.
      // For now, the redirect is in the success callback, so this block might not be strictly necessary
      // if the redirect is the final action for a successful delete.
      showModal.value = false; // Close modal
      return; // Exit function early as we've redirected or handled the delete fully.
    }
  }

  if (success) {
    if (newSelectedName) {
        // Determine if it's a create or copy operation for the isNewOrCopy flag
        const isCreatingOrCopying = type === 'create' || type === 'copy';
        await selectPreset(newSelectedName, isCreatingOrCopying); 
    }
  }
  if (success) { // [cite: 169, 175, 178, 186]
    // Message for rename is set here. Create/Copy messages are handled by fetchPresetData.
    if (type === 'rename') {
        editorMessage.value = `Preset '${inputValue}' renamed successfully.`;
        messageType.value = 'success';
    }
    showModal.value = false; // [cite: 169, 175, 178, 186]
  }
}

// Handles deletion of a preset (prompted by a direct button, not modal)
async function handleDeletePreset(name) { // [cite: 189]
  // Basic confirm, consider a more robust modal for this in future [cite: 189]
  if (!window.confirm(`Are you sure you want to delete preset '${name}'? This cannot be undone.`)) {
    return; // [cite: 189]
  }
  const success = await performApiCall(
    () => apiClient.deletePreset(name), // [cite: 191]
    () => {
      editorMessage.value = `Preset '${name}' deleted successfully.`; // [cite: 191]
      router.push('/');
      messageType.value = 'success'; // [cite: 191]
      if (selectedPresetName.value === name) { // [cite: 192]
        selectedPresetName.value = null; // Deselect if it was the deleted one [cite: 192]
        selectedPresetData.value = null; // [cite: 193]
      }
    },
    'Failed to delete preset' // [cite: 195]
  );
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