<template>
  <div class="container mx-auto px-0 sm:px-4 py-3 min-h-[calc(100vh-200px)]">
    <div v-if="editorMessage && messageType === 'error'"
      class="mb-6 mx-3 sm:mx-0 p-4 rounded-md text-sm text-center bg-red-700 text-red-100 transition-all duration-300"
      @click="clearMessage"> {{ editorMessage }}
    </div>

    <div class="flex flex-col gap-6">
      <div class="w-full">
        <Loading v-if="isLoadingData" message="Loading preset data..." />
        <div v-else-if="!selectedPresetName">
          <p class="text-center text-vybes-text-secondary">No preset selected</p>
        </div>
        <div v-else>
          <div class="flex items-center px-3 sm:px-0 mb-4">
            <h2 class="text-2xl font-semibold text-vybes-text-primary mr-2 truncate">
              {{ selectedPresetName }}
            </h2>
            <span v-if="selectedPresetData?.isCurrent" class="text-sm bg-vybes-accent text-vybes-dark font-medium px-2 py-1 rounded-md flex-none">Active</span>
            <div class="ml-2 flex space-x-1 flex-none">
              <button @click="openPresetModal('rename', selectedPresetName)" class="btn-icon icon-neutral" title="Rename preset" aria-label="Rename preset">
                <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M11 5H6a2 2 0 00-2 2v11a2 2 0 002 2h11a2 2 0 002-2v-5m-1.414-9.414a2 2 0 112.828 2.828L11.828 15H9v-2.828l8.586-8.586z"></path>
                </svg>
              </button>
              <button @click="openPresetModal('copy', selectedPresetName)" class="btn-icon icon-neutral" title="Copy preset" aria-label="Copy preset">
                <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M8 16H6a2 2 0 01-2-2V6a2 2 0 012-2h8a2 2 0 012 2v2m-6 12h8a2 2 0 002-2v-8a2 2 0 00-2-2h-8a2 2 0 00-2 2v8a2 2 0 002 2z"></path>
                </svg>
              </button>
              <button @click="openPresetModal('delete', selectedPresetName)" class="btn-icon icon-danger" title="Delete preset" aria-label="Delete preset">
                <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16"></path>
                </svg>
              </button>
            </div>
          </div>

          <CollapsibleSection title="EQ" :model-value="selectedPresetData.isPreferenceEQEnabled" @update:modelValue="updateEQEnabled($event)" :animate="animationsEnabled">
            <div :class="{ 'opacity-50 pointer-events-none': !selectedPresetData.isPreferenceEQEnabled }">
              <EQSection
                :eq-sets="prefEQSets"
                :preset-name="selectedPresetName"
                eq-type="pref"
                @update-eq-points="handleEQPointsUpdate('pref', $event)"
                :is-enabled="selectedPresetData.isPreferenceEQEnabled"
              />
            </div>
          </CollapsibleSection>

          <CollapsibleSection title="FIR Filters" :model-value="selectedPresetData.isFIREnabled" @update:modelValue="updateFIREnabled($event)" :animate="animationsEnabled">
            <div class="grid grid-cols-1 md:grid-cols-3 gap-6" :class="{ 'opacity-50': !selectedPresetData.isFIREnabled }">
              <template v-for="channel in FIR_CHANNELS" :key="channel.speaker">
                <SelectGroup
                  v-if="firFiles.length > 0"
                  :model-value="selectedPresetData[channel.key]"
                  :label="channel.label"
                  :disabled="!selectedPresetData.isFIREnabled"
                  @update:modelValue="updateFIRFilter(channel.speaker, $event)"
                >
                  <option value="">None</option>
                  <!-- Keep a configured file selectable even when it's no
                       longer on the SD card, so opening the editor can't
                       silently blank it -->
                  <option
                    v-if="selectedPresetData[channel.key] && !firFiles.includes(selectedPresetData[channel.key])"
                    :value="selectedPresetData[channel.key]"
                  >
                    {{ selectedPresetData[channel.key] }} (missing)
                  </option>
                  <option v-for="file in firFiles" :key="file" :value="file">{{ file }}</option>
                </SelectGroup>
                <!-- Fallback when the device reports no files (e.g. SD card
                     unavailable): keep the current value editable as text -->
                <InputGroup
                  v-else
                  :model-value="selectedPresetData[channel.key]"
                  :label="channel.label"
                  :disabled="!selectedPresetData.isFIREnabled"
                  @update:modelValue="updateFIRFilter(channel.speaker, $event)"
                />
              </template>
            </div>
            <div v-if="firFiles.length === 0" class="flex items-center gap-3 mt-4">
              <span class="text-sm text-vybes-text-secondary">No FIR files found on the device.</span>
              <button @click="loadFirFiles" class="btn-secondary">Refresh</button>
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

          <CollapsibleSection title="Volume & Balance" :toggleable="false" :animate="animationsEnabled">
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
      <p v-if="modalState.type === 'delete'" class="text-vybes-text-secondary mb-4">
        {{ modalState.message }}
      </p>
      <InputGroup
        v-else
        ref="modalInput"
        v-model="modalState.inputValue"
        :placeholder="modalState.placeholder"
        class="w-full mb-4"
        @keyup.enter="handleModalConfirm"
      />
    </ModalDialog>
  </div>
</template>

<script setup>
import { ref, reactive, onMounted, inject, onUnmounted, nextTick } from 'vue';
import { asyncDebounce } from '../utilities.js'; // Utility for rate limiting function calls
import RangeSlider from '../components/shared/RangeSlider.vue';
import InputGroup from '../components/shared/InputGroup.vue';
import SelectGroup from '../components/shared/SelectGroup.vue';
import ModalDialog from '../components/shared/ModalDialog.vue';
import SpeakerDelayInput from '../components/shared/SpeakerDelayInput.vue';
import VolumeControlGroup from '../components/shared/VolumeControlGroup.vue';
import EQSection from '../components/shared/EQSection.vue';
import CollapsibleSection from '../components/shared/CollapsibleSection.vue';
import Loading from '../components/shared/Loading.vue';
import { useRouter } from 'vue-router';

const router = useRouter();

// Define props for the component
const props = defineProps({
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

const apiClient = inject('vybesAPI'); // Injected API client for backend communication
const animationsEnabled = ref(false);
// General reactive state
const selectedPresetName = ref(null);
const selectedPresetData = ref(null); // Holds the full preset data object for the selected preset
const isLoadingData = ref(true); // True when fetching data for a selected preset
const editorMessage = ref(''); // Feedback message displayed to the user
const messageType = ref('info'); // Type of feedback message: 'error' or 'info'

// Unified Modal state
const modalState = reactive({
  type: null, // 'create', 'rename', 'copy', 'delete'
  title: '',
  confirmText: '',
  inputValue: '',
  placeholder: 'Preset Name',
  sourceName: '', // For rename/copy/delete operations
  message: '', // For confirmation-only modals (delete)
});
const showModal = ref(false); // Controls visibility of the unified modal
const modalInput = ref(null);

// Preset-specific editable properties
const speakerDelays = reactive({ left: 0, right: 0, sub: 0 });
const presetGains = reactive({ left: 100, right: 100, sub: 100 });

// FIR filter file selection (list comes from the Teensy's SD card)
const FIR_CHANNELS = [
  { speaker: 'left', key: 'firLeft', label: 'Left' },
  { speaker: 'right', key: 'firRight', label: 'Right' },
  { speaker: 'sub', key: 'firSub', label: 'Sub' },
];
const firFiles = ref([]);

async function loadFirFiles() {
  try {
    const files = await apiClient.getFirFiles();
    firFiles.value = Array.isArray(files) ? files : [];
  } catch (error) {
    // Not fatal: the UI falls back to free-text filename inputs
    console.error('Failed to load FIR file list:', error);
    firFiles.value = [];
  }
}

// EQ States
const prefEQSets = ref([]); // Array of preference curve EQ sets ({ spl: Number, peqSet: Array })
const currentPrefSPL = ref(0); // Default SPL value for preference curve

// Errors are worth interrupting for; routine successful tweaks are not, so
// continuous controls stay quiet on success.
const showError = (message) => {
  editorMessage.value = message;
  messageType.value = 'error';
  setTimeout(clearMessage, 5000);
};

// Clears the editor feedback message
const clearMessage = () => { editorMessage.value = ''; messageType.value = 'info'; };

// Generic helper to perform an API call and handle common success/failure patterns
async function performApiCall(apiCall, successCallback, failureMessage, failureCallback) {
  try {
    const response = await apiCall();
    if (successCallback) successCallback(response);
    return true; // Indicate success
  } catch (error) {
    console.error(failureMessage, error);
    showError(`${failureMessage}: ${error.message}`);
    if (failureCallback) failureCallback(error);
    return false; // Indicate failure
  }
}

// Each continuous control gets its own debounced instance: with a single
// shared debouncer, touching a second control within the wait window would
// silently cancel the first control's pending save.
const makeDebouncedApiCall = () => asyncDebounce(performApiCall, 500);
const debouncedCrossoverEnabledCall = makeDebouncedApiCall();
const debouncedCrossoverFreqCall = makeDebouncedApiCall();
const debouncedDelayEnabledCall = makeDebouncedApiCall();
const debouncedSpeakerDelayCalls = {
  left: makeDebouncedApiCall(),
  right: makeDebouncedApiCall(),
  sub: makeDebouncedApiCall(),
};
const debouncedPresetGainsCall = makeDebouncedApiCall();


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
    null,
    'Failed to update EQ points'
  );
};

const updateEQEnabled = async (value) => {
  selectedPresetData.value.isPreferenceEQEnabled = value;
  await performApiCall(() => apiClient.setEQEnabled(
    selectedPresetName.value,
    'pref',
    value
  ), null, 'Failed to update EQ setting', () => {
    selectedPresetData.value.isPreferenceEQEnabled = !value;
  });
};

const updateFIREnabled = async (value) => {
  selectedPresetData.value.isFIREnabled = value;
  await performApiCall(
    () => apiClient.updateFIREnabled(
      selectedPresetName.value,
      value
    ),
    null,
    'Failed to update FIR setting',
    () => {
      selectedPresetData.value.isFIREnabled = !value;
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
  ), null, 'Failed to update FIR filter');
};

const updateCrossoverEnabled = async (value) => {
  const currentValue = selectedPresetData.value.isCrossoverEnabled;
  selectedPresetData.value.isCrossoverEnabled = value;
  await debouncedCrossoverEnabledCall(() => apiClient.updateCrossoverEnabled(
    selectedPresetName.value,
    value
  ), null, 'Failed to update crossover setting', () => {
    selectedPresetData.value.isCrossoverEnabled = currentValue;
  });
};

let prevValue = null;
const updateCrossoverFreq = async (value) => {
  if (prevValue === null) prevValue = selectedPresetData.value.crossoverFreq;
  selectedPresetData.value.crossoverFreq = value;
  await debouncedCrossoverFreqCall(() => apiClient.updateCrossoverFreq(
    selectedPresetName.value,
    value
  ), () => {
    prevValue = null;
  }, 'Failed to update crossover frequency', () => {
    selectedPresetData.value.crossoverFreq = prevValue;
    prevValue = null;
  });
};

const updateSpeakerDelayEnabled = async (value) => {
  const currentValue = selectedPresetData.value.isSpeakerDelayEnabled;
  selectedPresetData.value.isSpeakerDelayEnabled = value;
  await debouncedDelayEnabledCall(() => apiClient.setSpeakerDelayEnabled(
    selectedPresetName.value,
    value
  ), null, 'Failed to update speaker delay setting', () => {
    selectedPresetData.value.isSpeakerDelayEnabled = currentValue;
  });
};

const updateSpeakerDelay = async (speaker, value) => {
  speakerDelays[speaker] = value;
  await debouncedSpeakerDelayCalls[speaker](() => apiClient.setSpeakerDelay(
    selectedPresetName.value,
    speaker,
    value
  ), null, 'Failed to update speaker delay');
};

const updatePresetGains = async (newGains) => {
  Object.assign(presetGains, newGains);
  await debouncedPresetGainsCall(() => apiClient.setPresetGains(
    selectedPresetName.value,
    newGains
  ), null, 'Failed to update preset gains');
};

// Fetch detailed data for a specific preset
async function fetchPresetData(presetName, isNewOrCopy = false) {
  if (!presetName) { selectedPresetData.value = null; isLoadingData.value = false; return; }
  isLoadingData.value = true; clearMessage();
  await performApiCall(
    () => apiClient.getPreset(presetName),
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
async function selectPreset(presetName, isNewOrCopy = false) {
  // Allow re-selection if isNewOrCopy is true, to ensure re-initialization logic runs for new/copied presets.
  if (selectedPresetName.value === presetName && selectedPresetData.value && !isNewOrCopy) return;
  selectedPresetName.value = presetName;
  await fetchPresetData(presetName, isNewOrCopy);
}

let unsubscribeLive = null;

// Component lifecycle hook
onMounted(async () => {
  loadFirFiles();
  await selectPreset(props.name);

  // Setup WebSocket listener for active preset changes
  unsubscribeLive = apiClient.connectLiveUpdates(
    (data) => {
      if (data.messageType === 'activePresetChanged') {
        router.push('/');
      }
    },
    (error) => {
      console.error('WebSocket error in PresetEditorView:', error);
    }
  );
});

onUnmounted(() => {
  if (unsubscribeLive) unsubscribeLive();
});

const MODAL_COPY = {
  create: { title: 'Create New Preset', confirmText: 'Create' },
  rename: { title: 'Rename Preset', confirmText: 'Rename' },
  copy: { title: 'Copy Preset', confirmText: 'Copy' },
  delete: { title: 'Delete Preset', confirmText: 'Delete' },
};

async function openPresetModal(type, name) {
  modalState.type = type;
  modalState.title = MODAL_COPY[type].title;
  modalState.confirmText = MODAL_COPY[type].confirmText;
  modalState.inputValue = type === 'copy' ? `${name} Copy` : name;
  modalState.sourceName = name;
  modalState.message = type === 'delete'
    ? `Delete preset "${name}"? This cannot be undone.`
    : '';
  showModal.value = true;

  if (type !== 'delete') {
    await nextTick();
    modalInput.value?.focus();
  }
}

// Handles the confirmation (submit) from the unified modal
async function handleModalConfirm() {
  const { type, inputValue, sourceName } = modalState;
  clearMessage();

  if (type === 'delete') {
    const success = await performApiCall(
      () => apiClient.deletePreset(sourceName),
      null,
      'Failed to delete preset'
    );
    showModal.value = false;
    if (success) {
      router.push('/');
    }
    return;
  }

  let newSelectedName = null; // The preset to select after the action

  const trimmedValue = inputValue.trim();
  if (!trimmedValue) {
    showError('Preset name cannot be empty.');
    return;
  }

  if (type === 'create') {
    await performApiCall(
      () => apiClient.createPreset(trimmedValue),
      () => { newSelectedName = trimmedValue; },
      'Failed to create preset'
    );
  } else if (type === 'rename') {
    if (trimmedValue === sourceName) {
      showModal.value = false;
      return;
    }
    await performApiCall(
      () => apiClient.renamePreset(sourceName, trimmedValue),
      () => { newSelectedName = trimmedValue; },
      'Failed to rename preset'
    );
  } else if (type === 'copy') {
    if (trimmedValue === sourceName) {
      showError('Copy name cannot be the same as the source name.');
      return;
    }
    await performApiCall(
      () => apiClient.copyPreset(sourceName, trimmedValue),
      () => { newSelectedName = trimmedValue; },
      'Failed to copy preset'
    );
  }

  if (newSelectedName) {
    await selectPreset(newSelectedName, true);
    await router.push(`/preset/${encodeURIComponent(newSelectedName)}`);
  }
  showModal.value = false;
}
</script>

<style scoped>
@reference "../style.css";

.icon-neutral {
  @apply bg-vybes-dark-element hover:bg-vybes-dark-input text-vybes-text-secondary hover:text-white;
}

.icon-danger {
  @apply bg-vybes-dark-element hover:bg-red-600 text-vybes-text-secondary hover:text-white;
}
</style>
