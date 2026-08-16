<template>
  <div class="container mx-auto px-0 sm:px-4 py-3 min-h-[calc(100vh-200px)]">
    <div v-if="displayedError"
      class="mb-6 mx-3 sm:mx-0 p-4 rounded-md text-sm text-center bg-red-700 text-red-100 transition-all duration-300"
      @click="dismissError"> {{ displayedError }}
    </div>

    <div class="flex flex-col gap-6">
      <div class="w-full">
        <Loading v-if="store.isLoading" message="Loading preset data..." />
        <div v-else-if="!store.preset">
          <p class="text-center text-vybes-text-secondary">No preset selected</p>
        </div>
        <div v-else>
          <!-- Header + tabs share the Tuning tab's cap so they stay aligned
               with the cards below; the Channels grid alone runs full width -->
          <div class="flex items-center px-3 sm:px-0 mb-4 max-w-3xl mx-auto">
            <h2 class="text-2xl font-semibold text-vybes-text-primary mr-2 truncate">
              {{ store.presetName }}
            </h2>
            <span v-if="store.preset.isCurrent" class="text-sm bg-vybes-accent text-vybes-dark font-medium px-2 py-1 rounded-md flex-none">Active</span>
            <span class="ml-2 text-xs bg-vybes-dark-element text-vybes-text-secondary font-medium px-2 py-1 rounded-md flex-none" title="Speaker setup template">
              {{ store.preset.template === 'custom' ? 'Custom' : store.preset.template }}
            </span>
            <div class="ml-2 flex space-x-1 flex-none">
              <button @click="openPresetModal('rename', store.presetName)" class="btn-icon icon-neutral" title="Rename preset" aria-label="Rename preset">
                <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M11 5H6a2 2 0 00-2 2v11a2 2 0 002 2h11a2 2 0 002-2v-5m-1.414-9.414a2 2 0 112.828 2.828L11.828 15H9v-2.828l8.586-8.586z"></path>
                </svg>
              </button>
              <button @click="openPresetModal('copy', store.presetName)" class="btn-icon icon-neutral" title="Copy preset" aria-label="Copy preset">
                <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M8 16H6a2 2 0 01-2-2V6a2 2 0 012-2h8a2 2 0 012 2v2m-6 12h8a2 2 0 002-2v-8a2 2 0 00-2-2h-8a2 2 0 00-2 2v8a2 2 0 002 2z"></path>
                </svg>
              </button>
              <button @click="openPresetModal('delete', store.presetName)" class="btn-icon icon-danger" title="Delete preset" aria-label="Delete preset">
                <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16"></path>
                </svg>
              </button>
            </div>
          </div>

          <!-- Tuning | Channels tabs -->
          <div class="flex gap-1 px-3 sm:px-0 mb-4 max-w-3xl mx-auto" role="tablist">
            <button
              v-for="tab in TABS" :key="tab.id"
              role="tab"
              :aria-selected="activeTab === tab.id"
              :class="['tab-button', activeTab === tab.id ? 'tab-active' : 'tab-inactive']"
              @click="activeTab = tab.id"
            >{{ tab.label }}</button>
          </div>

          <!-- ===== Tuning tab ===== -->
          <div v-if="activeTab === 'tuning'" class="max-w-3xl mx-auto">
            <CollapsibleSection title="EQ" :model-value="store.preset.inputEq.enabled" @update:modelValue="store.setInputEqEnabled($event)" :animate="animationsEnabled">
              <div :class="{ 'opacity-50 pointer-events-none': !store.preset.inputEq.enabled }">
                <EQSection
                  :eq-sets="store.inputEqSets"
                  :preset-name="store.presetName"
                  eq-type="pref"
                  @update-eq-points="store.saveInputEq($event)"
                  :is-enabled="store.preset.inputEq.enabled"
                />
              </div>
            </CollapsibleSection>

            <CollapsibleSection title="FIR Filters" :model-value="store.preset.firEnabled" @update:modelValue="store.setFirEnabled($event)" :animate="animationsEnabled">
              <div class="grid grid-cols-1 md:grid-cols-3 gap-6" :class="{ 'opacity-50': !store.preset.firEnabled }">
                <template v-for="output in store.enabledOutputs" :key="output.index">
                  <SelectGroup
                    v-if="store.firFiles.length > 0"
                    :model-value="output.fir"
                    :label="output.label"
                    :disabled="!store.preset.firEnabled"
                    @update:modelValue="store.setOutputFir(output.index, $event)"
                  >
                    <option value="">None</option>
                    <!-- Keep a configured file selectable even when it's no
                         longer on the SD card, so opening the editor can't
                         silently blank it -->
                    <option
                      v-if="output.fir && !store.firFiles.includes(output.fir)"
                      :value="output.fir"
                    >
                      {{ output.fir }} (missing)
                    </option>
                    <option v-for="file in store.firFiles" :key="file" :value="file">{{ file }}</option>
                  </SelectGroup>
                  <!-- Fallback when the device reports no files (e.g. SD card
                       unavailable): keep the current value editable as text -->
                  <InputGroup
                    v-else
                    :model-value="output.fir"
                    :label="output.label"
                    :disabled="!store.preset.firEnabled"
                    @update:modelValue="store.setOutputFir(output.index, $event)"
                  />
                </template>
              </div>
              <div class="flex items-center justify-between mt-4">
                <span class="text-sm text-vybes-text-secondary tabular-nums">
                  Taps used: {{ formatValue(store.firPool.used, '', 0) }} / {{ formatValue(store.firPool.total, '', 0) }}
                </span>
                <button v-if="store.firFiles.length === 0" @click="store.loadFirFiles" class="btn-secondary">Refresh</button>
              </div>
            </CollapsibleSection>

            <CrossoverCard :animate="animationsEnabled" />

            <CollapsibleSection title="Speaker Delays" :model-value="store.preset.delaysEnabled" @update:modelValue="store.setDelaysEnabled($event)" :animate="animationsEnabled">
              <div class="grid grid-cols-1 md:grid-cols-3 gap-3" :class="{ 'opacity-50': !store.preset.delaysEnabled }">
                <SpeakerDelayInput
                  v-for="output in store.enabledOutputs"
                  :key="output.index"
                  :title="output.label"
                  :model-value="output.delayUs"
                  :disabled="!store.preset.delaysEnabled"
                  @update:modelValue="store.setOutputDelay(output.index, $event)"
                />
              </div>
              <!-- The probe chirps the live outputs, so it only makes sense
                   on the preset that is actually playing -->
              <div v-if="store.preset.isCurrent" class="mt-4">
                <button class="btn-secondary" @click="delayWizardOpen = true">
                  Auto-align with phone mic…
                </button>
              </div>
              <p v-else class="mt-4 text-sm text-vybes-text-secondary">
                Activate this preset to auto-align delays with your phone's microphone.
              </p>
            </CollapsibleSection>

            <CollapsibleSection title="Output Levels" :toggleable="false" :animate="animationsEnabled">
              <div class="space-y-4">
                <RangeSlider
                  v-for="output in store.enabledOutputs"
                  :key="output.index"
                  :model-value="output.gainDb"
                  :label="output.label"
                  :min="-40"
                  :max="10"
                  :step="0.1"
                  unit="dB"
                  :decimals="1"
                  @update:modelValue="store.setOutputGain(output.index, $event)"
                />
              </div>
            </CollapsibleSection>
          </div>

          <!-- ===== Channels tab: the full 8-output matrix ===== -->
          <div v-else-if="activeTab === 'channels'" class="px-3 sm:px-0">
            <!-- Jump bar: on narrow screens the grid is one long column, so
                 reaching channel 7 by scrolling is the slow path -->
            <div class="lg:hidden sticky top-0 z-20 -mx-3 sm:mx-0 px-3 sm:px-0 py-2 bg-vybes-dark/95 backdrop-blur-sm">
              <div class="channel-rail">
                <button
                  v-for="output in store.outputs"
                  :key="output.index"
                  type="button"
                  class="channel-chip"
                  :class="{ 'channel-chip-active': expandedChannels.includes(output.index) }"
                  @click="focusChannel(output.index)"
                >
                  <span class="tabular-nums">{{ output.index + 1 }}</span> {{ output.label }}
                </button>
              </div>
            </div>

            <div class="mb-4">
              <FirPoolBar
                :used="store.firPool.used"
                :total="store.firPool.total"
                :errors="store.firPool.errors ?? []"
              />
            </div>
            <div class="grid grid-cols-1 lg:grid-cols-2 2xl:grid-cols-4 gap-4 items-start">
              <ChannelStrip
                v-for="output in store.outputs"
                :key="output.index"
                :id="`channel-${output.index}`"
                :output="output"
                :expanded="expandedChannels.includes(output.index)"
                @toggle="toggleChannel(output.index)"
              />
            </div>
          </div>
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
      <template v-else>
        <InputGroup
          ref="modalInput"
          v-model="modalState.inputValue"
          :placeholder="modalState.placeholder"
          class="w-full mb-4"
          @keyup.enter="handleModalConfirm"
        />
        <TemplateSelect v-if="modalState.type === 'create'" v-model="modalState.templateId" class="mb-4" />
      </template>
    </ModalDialog>

    <DelayAlignWizard v-model="delayWizardOpen" />
  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted, inject, onUnmounted, nextTick } from 'vue';
import InputGroup from '../components/shared/InputGroup.vue';
import SelectGroup from '../components/shared/SelectGroup.vue';
import ModalDialog from '../components/shared/ModalDialog.vue';
import RangeSlider from '../components/shared/RangeSlider.vue';
import CrossoverCard from '../components/shared/CrossoverCard.vue';
import ChannelStrip from '../components/shared/ChannelStrip.vue';
import FirPoolBar from '../components/shared/FirPoolBar.vue';
import TemplateSelect from '../components/shared/TemplateSelect.vue';
import SpeakerDelayInput from '../components/shared/SpeakerDelayInput.vue';
import DelayAlignWizard from '../components/DelayAlignWizard.vue';
import EQSection from '../components/shared/EQSection.vue';
import CollapsibleSection from '../components/shared/CollapsibleSection.vue';
import Loading from '../components/shared/Loading.vue';
import { useRouter } from 'vue-router';
import { usePresetStore } from '../stores/preset.js';
import { formatValue } from '../utilities.js';

const router = useRouter();
const store = usePresetStore();

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

const apiClient = inject('vybesAPI'); // CRUD (create/rename/copy/delete) stays view-local
const animationsEnabled = ref(false);

const TABS = [
  { id: 'tuning', label: 'Tuning' },
  { id: 'channels', label: 'Channels' },
];
const activeTab = ref('tuning');
const delayWizardOpen = ref(false);

// Channel accordion: all collapsed to start. Desktop can keep several open
// side by side; on phones the strips stack, so one at a time is enough.
const expandedChannels = ref([]);
const isNarrow = () => window.matchMedia('(max-width: 639px)').matches;

function toggleChannel(index) {
  if (expandedChannels.value.includes(index)) {
    expandedChannels.value = expandedChannels.value.filter((i) => i !== index);
  } else {
    expandedChannels.value = isNarrow() ? [index] : [...expandedChannels.value, index];
  }
}

async function focusChannel(index) {
  if (!expandedChannels.value.includes(index)) toggleChannel(index);
  await nextTick();
  document.getElementById(`channel-${index}`)?.scrollIntoView({
    behavior: 'smooth',
    block: 'start',
  });
}

// Local feedback (CRUD errors); everything store-driven surfaces store.error
const editorMessage = ref('');
const displayedError = computed(() => editorMessage.value || store.error);
const dismissError = () => { editorMessage.value = ''; store.clearError(); };

// Unified Modal state
const modalState = reactive({
  type: null, // 'create', 'rename', 'copy', 'delete'
  title: '',
  confirmText: '',
  inputValue: '',
  placeholder: 'Preset Name',
  sourceName: '', // For rename/copy/delete operations
  message: '', // For confirmation-only modals (delete)
  templateId: '2.1', // For create
});
const showModal = ref(false); // Controls visibility of the unified modal
const modalInput = ref(null);

// Errors are worth interrupting for; routine successful tweaks are not.
const showError = (message) => {
  editorMessage.value = message;
  setTimeout(() => { editorMessage.value = ''; }, 5000);
};

// Generic helper for the CRUD modal operations
async function performApiCall(apiCall, successCallback, failureMessage) {
  try {
    const response = await apiCall();
    if (successCallback) successCallback(response);
    return true;
  } catch (error) {
    console.error(failureMessage, error);
    showError(`${failureMessage}: ${error.message}`);
    return false;
  }
}

// Selects a preset by name
async function selectPreset(presetName, isNewOrCopy = false) {
  if (store.presetName === presetName && store.preset && !isNewOrCopy) return;
  await store.loadPreset(presetName);
  setTimeout(() => { animationsEnabled.value = true; }, 100);
}

let unsubscribeLive = null;

// Component lifecycle hook
onMounted(async () => {
  store.loadFirFiles();
  await selectPreset(props.name);

  unsubscribeLive = apiClient.connectLiveUpdates(
    (data) => {
      if (data.messageType === 'activePresetChanged') {
        router.push('/');
        return;
      }
      store.handleLiveMessage(data);
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
  editorMessage.value = '';

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
      () => apiClient.createPreset(trimmedValue, modalState.templateId),
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

.tab-button {
  @apply px-4 py-2 rounded-t-md text-sm font-medium transition-colors;
}

/* Blue: picking a tab is a selection, not a live state */
.tab-active {
  @apply bg-vybes-dark-element text-vybes-text-primary border-b-2 border-vybes-primary;
}

.tab-inactive {
  @apply text-vybes-text-secondary hover:text-vybes-text-primary hover:bg-vybes-dark-element/50;
}

/* Same chip language as the EQ band rail */
.channel-rail {
  @apply flex gap-2 overflow-x-auto pb-0.5;
  scrollbar-width: none;
  -webkit-overflow-scrolling: touch;
}

.channel-rail::-webkit-scrollbar {
  display: none;
}

.channel-chip {
  @apply flex-none rounded-full px-3 py-1.5 text-xs whitespace-nowrap cursor-pointer
         bg-vybes-dark-card border border-vybes-border text-vybes-text-secondary;
}

.channel-chip-active {
  @apply bg-vybes-dark-input text-vybes-text-primary border-vybes-primary;
}
</style>
