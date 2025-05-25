<template>
  <div class="container mx-auto p-6 bg-vybes-dark-element rounded-lg shadow-xl min-h-[calc(100vh-200px)]">
    <h1 class="text-3xl font-bold mb-6 text-vybes-light-blue text-center">Preset Editor</h1>

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

    <div class="flex flex-col md:flex-row gap-6">
      <!-- Preset List Column (Left) -->
      <div class="md:w-1/3 bg-vybes-dark-card p-5 rounded-lg shadow-md">
        <h2 class="text-xl font-semibold mb-4 text-vybes-accent">Presets</h2>
        <button @click="promptCreatePreset" class="btn-primary w-full mb-4">
          Create New Preset
        </button>

        <div v-if="isLoadingPresets" class="text-center py-4">
          <svg class="animate-spin h-6 w-6 text-vybes-primary mx-auto" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
            <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
            <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
          </svg>
          <p class="text-vybes-text-secondary text-sm">Loading presets...</p>
        </div>
        
        <ul v-else-if="presets.length > 0" class="space-y-2">
          <li v-for="preset in presets" :key="preset.name" 
              class="p-3 rounded-md transition-colors duration-150 ease-in-out cursor-pointer"
              :class="[
                selectedPresetName === preset.name ? 'bg-vybes-primary text-white shadow-lg' : 'bg-vybes-dark-input hover:bg-vybes-dark-hover',
                {'ring-2 ring-vybes-accent ring-offset-2 ring-offset-vybes-dark-card': preset.isCurrent && selectedPresetName !== preset.name}
              ]"
              @click="selectPreset(preset.name)">
            <div class="flex justify-between items-center">
              <span class="font-medium">{{ preset.name }} <span v-if="preset.isCurrent" class="text-xs text-vybes-accent">(Active)</span></span>
              <div class="space-x-1">
                <button @click.stop="promptRenamePreset(preset.name)" title="Rename" class="btn-icon btn-secondary-icon text-xs p-1">✏️</button>
                <button @click.stop="promptCopyPreset(preset.name)" title="Copy" class="btn-icon btn-secondary-icon text-xs p-1">📋</button>
                <button @click.stop="deletePreset(preset.name)" title="Delete" class="btn-icon btn-danger-icon text-xs p-1">🗑️</button>
              </div>
            </div>
          </li>
        </ul>
        <p v-else class="text-vybes-text-secondary text-sm text-center py-4">No presets found.</p>
      </div>

      <!-- Editing Area (Right) -->
      <div class="md:w-2/3 bg-vybes-dark-card p-5 rounded-lg shadow-md">
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
          <h2 class="text-2xl font-semibold mb-4 text-vybes-accent">
            Editing: {{ selectedPresetName }}
            <span v-if="selectedPresetData.isCurrent" class="text-base text-vybes-accent">(Active)</span>
          </h2>
          
          <!-- Tabs (Placeholder for future) -->
          <div class="mb-4 border-b border-vybes-dark-border">
            <nav class="flex space-x-4">
              <button @click="currentPANE = 'eq'" 
                      :class="['py-2 px-4 font-medium text-sm', currentPANE === 'eq' ? 'border-b-2 border-vybes-primary text-vybes-primary' : 'text-vybes-text-secondary hover:text-vybes-text-primary']">
                Parametric EQ
              </button>
              <!-- <button @click="currentPANE = 'crossover'" :class="[...]">Crossover</button> -->
              <!-- <button @click="currentPANE = 'delay'" :class="[...]">Delays</button> -->
            </nav>
          </div>

          <div v-if="currentPANE === 'eq'">
            <ParametricEQ 
              :peqPoints="eqPointsForEditor"
              @change="handleEQChange"
              :spl="selectedPresetData.eq?.room?.spl || 85"
              @splChange="handleSPLChange"
            />
            <div class="mt-4 text-right">
              <button @click="saveEQChanges" class="btn-primary">Save EQ Changes</button>
            </div>
          </div>
          <!-- Add other panes (crossover, delay) here later -->
        </div>
      </div>
    </div>

    <!-- Create Preset Modal -->
    <div v-if="showCreateModal" class="modal-backdrop">
      <div class="modal-content">
        <h3 class="text-xl font-semibold mb-4">Create New Preset</h3>
        <input type="text" v-model="newPresetName" placeholder="Preset Name" class="modal-input w-full mb-4">
        <div class="flex justify-end space-x-3">
          <button @click="showCreateModal = false" class="btn-secondary">Cancel</button>
          <button @click="createPreset" class="btn-primary">Create</button>
        </div>
      </div>
    </div>
    
    <!-- Rename Preset Modal -->
    <div v-if="showRenameModal" class="modal-backdrop">
      <div class="modal-content">
        <h3 class="text-xl font-semibold mb-4">Rename Preset '{{ selectedPresetName }}'</h3>
        <input type="text" v-model="renamePresetName" placeholder="New Preset Name" class="modal-input w-full mb-4">
        <div class="flex justify-end space-x-3">
          <button @click="showRenameModal = false" class="btn-secondary">Cancel</button>
          <button @click="renamePreset" class="btn-primary">Rename</button>
        </div>
      </div>
    </div>

    <!-- Copy Preset Modal -->
    <div v-if="showCopyModal" class="modal-backdrop">
      <div class="modal-content">
        <h3 class="text-xl font-semibold mb-4">Copy Preset '{{ copyPresetSourceName }}'</h3>
        <input type="text" v-model="copyPresetNewName" placeholder="New Preset Name" class="modal-input w-full mb-4">
        <div class="flex justify-end space-x-3">
          <button @click="showCopyModal = false" class="btn-secondary">Cancel</button>
          <button @click="copyPreset" class="btn-primary">Copy</button>
        </div>
      </div>
    </div>

  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted, watch, inject } from 'vue';
import ParametricEQ from '../components/ParametricEQ.vue'; // Adjust path as needed

const apiClient = inject('vybesAPI');
const liveUpdateData = inject('liveUpdateData'); // To be used later

const presets = ref([]);
const selectedPresetName = ref(null);
const selectedPresetData = ref(null); // Holds the full preset data object
const isLoadingPresets = ref(false);
const isLoadingData = ref(false);
const editorMessage = ref('');
const messageType = ref('info'); // 'success', 'error', 'info'

const showCreateModal = ref(false);
const newPresetName = ref('');
const showRenameModal = ref(false);
const renamePresetName = ref('');
const showCopyModal = ref(false);
const copyPresetSourceName = ref('');
const copyPresetNewName = ref('');

const currentPANE = ref('eq'); // For now, only 'eq'

// Computed property for EQ points to pass to the ParametricEQ component
const eqPointsForEditor = computed(() => {
  // Ensure this path matches your actual data structure
  return selectedPresetData.value?.eq?.room?.peqSet || [];
});

function clearMessage() {
  editorMessage.value = '';
  messageType.value = 'info';
}

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

function selectPreset(presetName) {
  if (selectedPresetName.value === presetName && selectedPresetData.value) return; // Avoid refetch if already selected
  selectedPresetName.value = presetName;
  // fetchPresetData will be called by the watcher on selectedPresetName
}

// Called by ParametricEQ component
// This function updates the local state. A separate save function will send to backend.
function handleEQChange(newEqPoints) {
  if (!selectedPresetData.value || !selectedPresetData.value.eq || !selectedPresetData.value.eq.room) {
    // Initialize structure if it doesn't exist
    if (!selectedPresetData.value) selectedPresetData.value = {};
    if (!selectedPresetData.value.eq) selectedPresetData.value.eq = {};
    if (!selectedPresetData.value.eq.room) selectedPresetData.value.eq.room = { spl: 85, peqSet: [] }; // Default SPL
  }
  selectedPresetData.value.eq.room.peqSet = newEqPoints;
  editorMessage.value = 'EQ changes are pending. Click "Save EQ Changes" to apply.';
  messageType.value = 'info';
}

function handleSPLChange(newSPL) {
    if (selectedPresetData.value && selectedPresetData.value.eq && selectedPresetData.value.eq.room) {
        selectedPresetData.value.eq.room.spl = newSPL;
        editorMessage.value = 'SPL change is pending. Click "Save EQ Changes" to apply.';
        messageType.value = 'info';
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


onMounted(() => {
  fetchPresets();
});

watch(selectedPresetName, (newName, oldName) => {
  if (newName && newName !== oldName) {
    fetchPresetData(newName);
  } else if (!newName) {
    selectedPresetData.value = null; // Clear data if no preset is selected
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
/* Basic Modal Styling */
.modal-backdrop {
  @apply fixed inset-0 bg-black bg-opacity-60 flex items-center justify-center p-4 transition-opacity duration-300 ease-in-out;
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
  background: theme('colors.vybes-dark.input');
  border-radius: 4px;
}
.preset-list-container::-webkit-scrollbar-thumb {
  background: theme('colors.vybes.primary');
  border-radius: 4px;
}
.preset-list-container::-webkit-scrollbar-thumb:hover {
  background: theme('colors.vybes.primary-hover');
}
</style>
