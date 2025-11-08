<template>
  <div class="container mx-auto p-3 bg-vybes-dark-element rounded-lg shadow-xl min-h-[calc(100vh-200px)]">
    <div v-if="editorMessage && messageType === 'error'" class="mb-6 p-4 rounded-md text-sm text-center transition-all duration-300"
      :class="{
        'bg-green-700 text-green-100': messageType === 'success',
        'bg-red-700 text-red-100': messageType === 'error',
        'bg-blue-700 text-blue-100': messageType === 'info', // [cite: 2]
      }"
      @click="clearMessage"> {{ editorMessage }}
    </div>

    <div class="flex flex-col gap-6">
      <div class="w-full">
        <Loading v-if="isLoadingData" message="Loading preset data..." />
        <div v-else-if="!selectedPresetName">
          <p class="text-center text-vybes-text-secondary">No preset selected</p>
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
          
          <CollapsibleSection title="EQ" :model-value="selectedPresetData.isPreferenceEQEnabled" @update:modelValue="updateEQEnabled($event)" :animate="animationsEnabled">
            <EQSection
              :eq-sets="prefEQSets"
              :preset-name="selectedPresetName"
              eq-type="pref"
              @update-eq-points="handleEQPointsUpdate('pref', $event)"
              @create-new-set="handleCreateNewSet('pref', $event)"
              :is-enabled="selectedPresetData.isPreferenceEQEnabled"
            />
          </CollapsibleSection>

          <CollapsibleSection title="FIR Filters" :model-value="selectedPresetData.isFIREnabled" @update:modelValue="updateFIREnabled($event)" :animate="animationsEnabled">
            <div class="grid grid-cols-1 md:grid-cols-3 gap-6" :class="{ 'opacity-50': !selectedPresetData.isFIREnabled }">
              <InputGroup 
                v-model="selectedPresetData.firLeft" 
                :label="'Left'"
                :disabled="!selectedPresetData.isFIREnabled"
                @update:modelValue="updateFIRFilter('left', $event)"
              />
              <InputGroup 
                v-model="selectedPresetData.firRight" 
                :label="'Right'"
                :disabled="!selectedPresetData.isFIREnabled"
                @update:modelValue="updateFIRFilter('right', $event)"
              />
              <InputGroup 
                v-model="selectedPresetData.firSub" 
                :label="'Sub'"
                :disabled="!selectedPresetData.isFIREnabled"
                @update:modelValue="updateFIRFilter('sub', $event)"
              />
            </div>
          </CollapsibleSection>

          <CollapsibleSection title="Subwoofer Crossover" :model-value="selectedPresetData.isCrossoverEnabled" @update:modelValue="updateCrossoverEnabled($event)" :animate="animationsEnabled">
            <div :class="{ 'opacity-50': !selectedPresetData.isCrossoverEnabled }">
              <RangeSlider
                :model-value="Number(selectedPresetData.crossoverFreq)"
                label="Frequency" :min="40"
                :max="500"
                :step="5"
                unit="Hz"
                :decimals="0"
                @update:modelValue="updateCrossoverFreq($event)"
              />
            </div>
          </CollapsibleSection>

          <CollapsibleSection title="Speaker Delays" :model-value="selectedPresetData.isSpeakerDelayEnabled" @update:modelValue="updateSpeakerDelayEnabled($event)" :animate="animationsEnabled">
            <div class="grid grid-cols-1 md:grid-cols-3 gap-3" :class="{ 'opacity-50': !selectedPresetData.isSpeakerDelayEnabled }">
              <SpeakerDelayInput 
                title="Left" 
                v-model="speakerDelays.left" 
                :disabled="!selectedPresetData.isSpeakerDelayEnabled"
                @update:modelValue="updateSpeakerDelay('left', $event)"
              />
              <SpeakerDelayInput 
                title="Right" 
                v-model="speakerDelays.right"
                :disabled="!selectedPresetData.isSpeakerDelayEnabled"
                @update:modelValue="updateSpeakerDelay('right', $event)"
              />
              <SpeakerDelayInput 
                title="Sub" 
                v-model="speakerDelays.sub"
                :disabled="!selectedPresetData.isSpeakerDelayEnabled"
                @update:modelValue="updateSpeakerDelay('sub', $event)"
              />
            </div>
          </CollapsibleSection>

          <CollapsibleSection title="Volume & Balance" v-model="volumeAndBalanceExpanded" :animate="animationsEnabled">
            <VolumeControlGroup
              :initial-gains="presetGains"
              @update:gains="updatePresetGains"
            />
          </CollapsibleSection>
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
import { ref, reactive, onMounted, inject, onUnmounted } from 'vue';
import { asyncDebounce } from '../utilities.js'; // [cite: 42] Utility for rate limiting function calls
import RangeSlider from '../components/shared/RangeSlider.vue';
import InputGroup from '../components/shared/InputGroup.vue';
import ModalDialog from '../components/shared/ModalDialog.vue';
import SpeakerDelayInput from '../components/shared/SpeakerDelayInput.vue'; // New component for speaker delay inputs
import VolumeControlGroup from '../components/shared/VolumeControlGroup.vue';
import EQSection from '../components/shared/EQSection.vue'; // New component for EQ sections
import CollapsibleSection from '../components/shared/CollapsibleSection.vue';
import Loading from '../components/shared/Loading.vue';
import ToggleSwitch from '../components/shared/ToggleSwitch.vue';
import { useRouter } from 'vue-router';

const router = useRouter();

// Define props for the component
const props = defineProps({ // [cite: 44]
  initialPreset: {
    type: String,
    required: false,
    default: null
  },
  isNew: {
    type: Boolean,
    default: false
  },
  isCopy: {
    type: Boolean,
    default: false
  },
  copySourcePreset: {
    type: String,
    default: ''
  },
  name: {
    type: String,
    default: ''
  }
});

const apiClient = inject('vybesAPI'); // Injected API client for backend communication [cite: 45] 
const prefEQExpanded = ref(false);
const crossoverExpanded = ref(true);
const speakerDelayExpanded = ref(false);
const volumeAndBalanceExpanded = ref(true);
const animationsEnabled = ref(false);
// General reactive state
const selectedPresetName = ref(null); // Selected preset name [cite: 45]
const selectedPresetData = ref(null); // Holds the full preset data object for the selected preset [cite: 45]
const isLoadingData = ref(true); // True when fetching data for a selected preset [cite: 46]
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
const presetGains = reactive({ left: 100, right: 100, sub: 100 });
const crossoverFreq = ref(80); // Crossover frequency, Default 80Hz [cite: 50]

// EQ States
const prefEQSets = ref([]); // Array of preference curve EQ sets ({ spl: Number, peqSet: Array }) [cite: 59]
const currentPrefSPL = ref(0); // Default SPL value for preference curve [cite: 59]

// Helper to get reactive refs for a given EQ type ('room' or 'pref')
const getEQRefs = { sets: prefEQSets, currentSPL: currentPrefSPL, type: 'pref' };

// Helper functions for showing messages
const showSuccess = (message) => {
  editorMessage.value = message;
  messageType.value = 'success';
  setTimeout(clearMessage, 3000);
};

const showError = (message) => {
  editorMessage.value = message;
  messageType.value = 'error';
  setTimeout(clearMessage, 5000);
};

// Clears the editor feedback message
const clearMessage = () => { editorMessage.value = ''; messageType.value = 'info'; }; // [cite: 62]

// Generic helper to perform an API call and handle common success/failure patterns
async function performApiCall(apiCall, successCallback, failureMessage, failureCallback) {
  try {
    const response = await apiCall();
    if (successCallback) successCallback(response);
    return true; // Indicate success
  } catch (error) {
    console.error(failureMessage, error);
    editorMessage.value = `${failureMessage}: ${error.message}`;
    messageType.value = 'error';
    if (failureCallback) failureCallback(error);
    return false; // Indicate failure
  }
}

const debouncedApiCall = asyncDebounce(async (apiCall, successCallback, failureMessage, failureCallback) => {
  return await performApiCall(apiCall, successCallback, failureMessage, failureCallback);
}, 500);


// API methods for updating preset settings
const handleEQPointsUpdate = async (type, updatedPoints) => {
  if (!selectedPresetData.value || !prefEQSets.value || prefEQSets.value.length === 0) return;

  // Based on the logic in EQSection.vue, we can assume we are always editing the first set.
  const targetSet = prefEQSets.value[0];

  // Update local state for immediate UI response by replacing the points in the target set.
  // This preserves the overall data structure and prevents the component from re-rendering.
  targetSet.peqs = updatedPoints;

  await performApiCall(
    () => apiClient.savePrefEqSet(selectedPresetName.value, updatedPoints),
    () => {
      showSuccess('EQ points updated');
    },
    'Failed to update EQ points',
    () => {
      showError('Failed to update EQ points');
    }
  );
};

const updateEQEnabled = async (value) => {
  selectedPresetData.value.isPreferenceEQEnabled = value;
  await performApiCall(() => apiClient.setEQEnabled(
    selectedPresetName.value,
    'pref',
    value
  ), () => {
    showSuccess('Preference EQ setting updated');
  }, 'Failed to update EQ setting', () => {
    showError('Failed to update EQ setting');
    selectedPresetData.value.isPreferenceEQEnabled = !selectedPresetData.value.isPreferenceEQEnabled;
  });
};

const updateFIREnabled = async (value) => {
  selectedPresetData.value.isFIREnabled = value;
  await performApiCall(
    () => apiClient.updateFIREnabled(
      selectedPresetName.value,
      value
    ),
    () => showSuccess('FIR filter setting updated'),
    'Failed to update FIR setting',
    () => {
      showError('Failed to update FIR setting');
      selectedPresetData.value.isFIREnabled = !selectedPresetData.value.isFIREnabled;
    }
  );
};

const updateFIRFilter = async (speaker, value) => {
  const key = `fir${speaker.charAt(0).toUpperCase() + speaker.slice(1)}`;
  selectedPresetData.value[key] = value;
  await performApiCall(() => apiClient.setFirFilter(
    selectedPresetName.value,
    speaker,
    value
  ), () => {
    showSuccess(`${speaker} FIR filter updated`);
  }, 'Failed to update FIR filter', () => {
    showError('Failed to update FIR filter');
  });
};

const updateCrossoverEnabled = async (value) => {
  const currentValue = selectedPresetData.value.isCrossoverEnabled;
  selectedPresetData.value.isCrossoverEnabled = value;
  await debouncedApiCall(() => apiClient.updateCrossoverEnabled(
    selectedPresetName.value,
    value
  ), () => {
    showSuccess('Crossover setting updated');
  }, 'Failed to update crossover setting', () => {
    showError('Failed to update crossover setting');
    selectedPresetData.value.isCrossoverEnabled = currentValue;
  });
};

let prevValue = null;
const updateCrossoverFreq = async (value) => {
  if (prevValue === null) prevValue = selectedPresetData.value.crossoverFreq;
  selectedPresetData.value.crossoverFreq = value;
  await debouncedApiCall(() => apiClient.updateCrossoverFreq(
    selectedPresetName.value,
    value
  ), () => {
    showSuccess('Crossover frequency updated');
    prevValue = null;
  }, 'Failed to update crossover frequency', () => {
    showError('Failed to update crossover frequency');
    selectedPresetData.value.crossoverFreq = prevValue;
    prevValue = null;
  });
};

const updateSpeakerDelayEnabled = async (value) => {
  const currentValue = selectedPresetData.value.isSpeakerDelayEnabled;
  selectedPresetData.value.isSpeakerDelayEnabled = value;
  await debouncedApiCall(() => apiClient.setSpeakerDelayEnabled(
    selectedPresetName.value,
    value
  ), () => {
    showSuccess('Speaker delay setting updated');
  }, 'Failed to update speaker delay setting', () => {
    showError('Failed to update speaker delay setting');
    selectedPresetData.value.isSpeakerDelayEnabled = currentValue;
  });
};

const updateSpeakerDelay = async (speaker, value) => {
  speakerDelays[speaker] = value;
  await debouncedApiCall(() => apiClient.setSpeakerDelay(
    selectedPresetName.value,
    speaker,
    value
  ), () => {
    showSuccess(`${speaker} speaker delay updated`);
  }, 'Failed to update speaker delay', () => {
    showError('Failed to update speaker delay');
  });
};

const updatePresetGains = async (newGains) => {
  Object.assign(presetGains, newGains);
  await debouncedApiCall(() => apiClient.setPresetGains(
    selectedPresetName.value,
    newGains
  ), () => {
    showSuccess('Preset gains updated');
  }, 'Failed to update preset gains', () => {
    showError('Failed to update preset gains');
  });
};

// Save crossover enabled (not debounced)
const setCrossoverEnabled = async () => {
  if (!selectedPresetName.value) return;
  const currentValue = selectedPresetData.value?.crossover.enabled;
  await debouncedApiCall(() => apiClient.setCrossoverEnabled(selectedPresetName.value, crossoverEnabled.value),
    null,
    'Failed to set crossover enabled',
    () => {
      // Reset to previous value on failure
      crossoverEnabled.value = currentValue;
    }
  );
};

// Fetch detailed data for a specific preset
async function fetchPresetData(presetName, isNewOrCopy = false) { // [cite: 72]
  if (!presetName) { selectedPresetData.value = null; isLoadingData.value = false; return; } // [cite: 72]
  isLoadingData.value = true; clearMessage(); // [cite: 73]
  await performApiCall(
    () => apiClient.getPreset(presetName), // [cite: 74]
    (data) => {
      selectedPresetData.value = data;
      Object.assign(speakerDelays, data.speakerDelays);
      apiClient.getPresetGains(presetName).then(gains => {
        Object.assign(presetGains, gains);
      });
      
      // Initialize prefEQSets from the preference curve data
      if (data.preferenceEQ) {
        prefEQSets.value = data.preferenceEQ;
        if (prefEQSets.value.length > 0) {
          currentPrefSPL.value = prefEQSets.value[0].spl;
        }
      } else {
        // Default empty set if no preference curve data exists
        prefEQSets.value = [];
        currentPrefSPL.value = 0;
      }
    },
    `Failed to load data for '${presetName}'`,
    () => {
      selectedPresetData.value = null;
      prefEQSets.value = [];
      currentPrefSPL.value = 0;
    }
  );
  isLoadingData.value = false; 
  setTimeout(() => {
    animationsEnabled.value = true;
  }, 100);
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

  // Setup WebSocket listener for active preset changes
  console.log('PresetEditorView: apiClient defined?', !!apiClient);
  apiClient.connectLiveUpdates(
    (data) => {
      if (data.messageType === 'activePresetChanged') {
        console.log('Active preset changed, redirecting to home:', data.activePresetName);
        router.push('/');
      }
    },
    (error) => {
      console.error('WebSocket error in PresetEditorView:', error);
    },
    () => {
      console.log('WebSocket disconnected in PresetEditorView');
    }
  );
});

onUnmounted(() => {
  apiClient.disconnectLiveUpdates();
});

async function openPresetModal(type, name) {
  modalState.type = type;
  modalState.title = type === 'create' ? 'Create New Preset' : 'Rename Preset';
  modalState.confirmText = type === 'create' ? 'Create' : 'Rename';
  modalState.inputValue = name;
  modalState.sourceName = name;
  showModal.value = true;

  //On next tick, focus the input field and select all text
  nextTick(() => {
    newPresetNameInput.value.focus();
    newPresetNameInput.value.select();
  });
}

// Handles the confirmation (submit) from the unified modal
async function handleModalConfirm() {
  const { type, inputValue, sourceName } = modalState;
  editorMessage.value = ''; // Clear message before operation
  let newSelectedName = null; // To store the name of the preset to be selected after action

  const trimmedValue = inputValue.trim();
  if (!trimmedValue) {
    editorMessage.value = 'Preset name cannot be empty.'; // [cite: 167, 173, 183]
    messageType.value = 'error'; // [cite: 168, 174, 184]
    return;
  }

  if (type === 'create') {
    await performApiCall(
      () => apiClient.createPreset(trimmedValue), // [cite: 169]
      () => { editorMessage.value = `Preset '${trimmedValue}' created successfully.`; messageType.value = 'success'; newSelectedName = trimmedValue; }, // [cite: 169]
      'Failed to create preset',
      () => {
        editorMessage.value = 'Failed to create preset';
        messageType.value = 'error';
      }
    );
  } else if (type === 'rename') {
    if (trimmedValue === sourceName) { // [cite: 174]
      editorMessage.value = 'New name is the same as the old name.'; // [cite: 174]
      messageType.value = 'info'; // [cite: 175]
    }
    await performApiCall(
      () => apiClient.renamePreset(sourceName, trimmedValue), // [cite: 177]
      () => { editorMessage.value = `Preset '${sourceName}' renamed to '${trimmedValue}'.`; messageType.value = 'success'; newSelectedName = trimmedValue; }, // [cite: 177]
      'Failed to rename preset' // [cite: 181]
    );
  } else if (type === 'copy') {
    if (trimmedValue === sourceName) { // [cite: 184]
      editorMessage.value = 'Copy name cannot be the same as the source name.'; // [cite: 184]
      messageType.value = 'error'; // [cite: 185]
    }
    await performApiCall(
      () => apiClient.copyPreset(sourceName, trimmedValue), // [cite: 186]
      () => { editorMessage.value = `Preset '${sourceName}' copied to '${trimmedValue}'.`; messageType.value = 'success'; newSelectedName = trimmedValue; }, // [cite: 186]
      'Failed to copy preset',
      () => {
        editorMessage.value = 'Failed to copy preset';
        messageType.value = 'error';
      }
    );
  } else if (type === 'delete') {
    await performApiCall(
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
    showModal.value = false; // Close modal
    return; // Exit function early as we've redirected or handled the delete fully.
  }

  if (newSelectedName) {
    await selectPreset(newSelectedName);
    await router.push(`/preset/${newSelectedName}`);
  }
  // Message for rename is set here. Create/Copy messages are handled by fetchPresetData.
  if (type === 'rename') {
    editorMessage.value = `Preset '${inputValue}' renamed successfully.`;
    messageType.value = 'success';
  }
  showModal.value = false;
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