/*
 * Central store for the preset being edited: the V1 config (8 output
 * channels, shared crossover points, input EQ, FIR pool) plus the device
 * file/template lists. Crossover points fan out to multiple outputs and the
 * FIR pool is global, so this state is cross-cutting by design - views
 * render from here and websocket broadcasts are merged in here.
 *
 * Mutation pattern: actions update local state immediately (optimistic),
 * then push to the API. Continuous controls share a per-key debouncer so
 * dragging one slider can't cancel another's pending save. On failure the
 * preset is refetched so the UI resyncs to the device's actual state, and
 * `error` carries the message for the view to display.
 */
import { defineStore } from 'pinia';
import { ref, computed } from 'vue';
import apiClient from '../api-client.js';
import { asyncDebounce } from '../utilities.js';

export const usePresetStore = defineStore('preset', () => {
  // ===== State =====
  const presetName = ref(null);
  const preset = ref(null); // full V1 preset object from GET /preset
  const isLoading = ref(false);
  const error = ref('');
  const firFiles = ref([]);
  const templates = ref([]);

  // ===== Getters =====
  const outputs = computed(() =>
    preset.value ? preset.value.outputs.map((o, index) => ({ ...o, index })) : []
  );
  const enabledOutputs = computed(() => outputs.value.filter((o) => o.enabled));
  const crossovers = computed(() => preset.value?.crossovers ?? []);
  const firPool = computed(() => preset.value?.firPool ?? { total: 0, used: 0 });
  // Shape the EQSection component expects: [{spl, peqs}]
  const inputEqSets = computed(() =>
    preset.value ? preset.value.inputEq.sets.map((s) => ({ spl: s.spl, peqs: s.points })) : []
  );

  /** A crossover point is "enabled" while any filter references it in xover mode */
  function isCrossoverEnabled(id) {
    if (!preset.value) return false;
    return preset.value.outputs.some(
      (o) =>
        (o.hp.xover === id && o.hp.mode === 'xover') ||
        (o.lp.xover === id && o.lp.mode === 'xover')
    );
  }

  /** Outputs whose HP or LP references the given crossover point */
  function outputsReferencing(id) {
    return outputs.value.filter((o) => o.hp.xover === id || o.lp.xover === id);
  }

  // ===== Error handling =====
  let errorTimer = null;
  function fail(message, e) {
    console.error(message, e);
    error.value = e?.message ? `${message}: ${e.message}` : message;
    clearTimeout(errorTimer);
    errorTimer = setTimeout(() => { error.value = ''; }, 6000);
  }
  function clearError() {
    clearTimeout(errorTimer);
    error.value = '';
  }

  // ===== Loading =====
  async function loadPreset(name) {
    isLoading.value = true;
    clearError();
    try {
      preset.value = await apiClient.getPreset(name);
      presetName.value = name;
    } catch (e) {
      preset.value = null;
      presetName.value = null;
      fail(`Failed to load preset '${name}'`, e);
    } finally {
      isLoading.value = false;
    }
  }

  /** Re-sync from the device (used after failed optimistic updates) */
  async function refresh() {
    if (!presetName.value) return;
    try {
      preset.value = await apiClient.getPreset(presetName.value);
    } catch (e) {
      // The failure that triggered the refresh is already displayed
      console.error('Preset refresh failed:', e);
    }
  }

  async function loadFirFiles() {
    try {
      const files = await apiClient.getFirFiles();
      firFiles.value = Array.isArray(files) ? files : [];
    } catch (e) {
      // Not fatal: FIR selectors fall back to free-text inputs
      console.error('Failed to load FIR file list:', e);
      firFiles.value = [];
    }
  }

  async function loadTemplates() {
    try {
      templates.value = await apiClient.getTemplates();
    } catch (e) {
      console.error('Failed to load template list:', e);
      templates.value = [];
    }
  }

  // ===== Mutation plumbing =====

  // One debouncer per control key: touching a second control within the
  // wait window must not cancel the first one's pending save.
  const debouncers = new Map();
  function debounced(key, fn) {
    if (!debouncers.has(key)) debouncers.set(key, asyncDebounce((f) => f(), 400));
    return debouncers.get(key)(fn);
  }

  /** Push an already-applied optimistic change; refetch to resync on failure. */
  async function push(call, failureMessage) {
    try {
      await call();
      return true;
    } catch (e) {
      fail(failureMessage, e);
      await refresh();
      return false;
    }
  }

  const pushDebounced = (key, call, failureMessage) =>
    debounced(key, () => push(call, failureMessage));

  // ===== Output actions =====

  function setOutput(index, changes) {
    Object.assign(preset.value.outputs[index], changes);
  }

  function setOutputDelay(index, delayUs) {
    setOutput(index, { delayUs });
    return pushDebounced(`delay:${index}`,
      () => apiClient.setOutputDelay(presetName.value, index, delayUs),
      'Failed to update delay');
  }

  function setOutputGain(index, gainDb) {
    setOutput(index, { gainDb });
    return pushDebounced(`gain:${index}`,
      () => apiClient.setOutputGain(presetName.value, index, gainDb),
      'Failed to update output gain');
  }

  async function setOutputFir(index, file) {
    setOutput(index, { fir: file });
    return push(async () => {
      const response = await apiClient.setOutputFir(presetName.value, index, file);
      if (response.firPool) preset.value.firPool = response.firPool;
    }, 'Failed to update FIR filter');
  }

  function setOutputMute(index, mute) {
    setOutput(index, { mute });
    return push(() => apiClient.setOutputMute(presetName.value, index, mute),
      'Failed to update mute');
  }

  function setOutputInvert(index, invert) {
    setOutput(index, { invert });
    return push(() => apiClient.setOutputInvert(presetName.value, index, invert),
      'Failed to update polarity');
  }

  function setOutputEnabled(index, enabled) {
    setOutput(index, { enabled });
    return push(() => apiClient.setOutputEnabled(presetName.value, index, enabled),
      'Failed to update output');
  }

  function setOutputLabel(index, label) {
    setOutput(index, { label });
    return pushDebounced(`label:${index}`,
      () => apiClient.setOutputLabel(presetName.value, index, label),
      'Failed to update label');
  }

  function setOutputSource(index, source) {
    setOutput(index, { source });
    return pushDebounced(`source:${index}`,
      () => apiClient.setOutputSource(presetName.value, index, source),
      'Failed to update source mix');
  }

  /** HP/LP edits are not optimistic: the server owns the hpFloor verdict. */
  async function setOutputFilter(index, which, filter) {
    const ok = await push(async () => {
      const response = await apiClient.setOutputFilter(presetName.value, index, which, filter);
      setOutput(index, response.changes);
      if (response.template) preset.value.template = response.template;
    }, 'Filter change rejected');
    return ok;
  }

  function saveOutputEq(index, points) {
    setOutput(index, { peq: points });
    return push(() => apiClient.saveOutputEq(presetName.value, index, points),
      'Failed to update output EQ');
  }

  // ===== Crossover actions =====

  /**
   * Set a crossover point's frequency. Unlocked points apply optimistically
   * (slider drags); locked points must pass confirm=true and are never
   * optimistic - the caller awaits the server's verdict (409 on missing
   * confirmation or an hpFloor violation).
   */
  async function setCrossoverFreq(id, freq, confirm = false) {
    const point = preset.value.crossovers.find((x) => x.id === id);
    if (!point) return false;
    if (!point.locked) {
      point.freq = freq;
      return pushDebounced(`xover:${id}`,
        () => apiClient.setCrossoverPointFreq(presetName.value, id, freq, confirm),
        'Failed to update crossover');
    }
    try {
      clearError();
      await apiClient.setCrossoverPointFreq(presetName.value, id, freq, confirm);
      point.freq = freq;
      return true;
    } catch (e) {
      fail('Crossover change rejected', e);
      return false;
    }
  }

  /** Bypass/enable a crossover point on every filter referencing it. */
  async function setCrossoverEnabled(id, enabled, confirm = false) {
    return push(async () => {
      await apiClient.setCrossoverPointEnabled(presetName.value, id, enabled, confirm);
      // Mirror the server's toggle locally
      for (const output of preset.value.outputs) {
        for (const which of ['hp', 'lp']) {
          const filter = output[which];
          if (filter.xover === id && ['xover', 'off'].includes(filter.mode)) {
            filter.mode = enabled ? 'xover' : 'off';
          }
        }
      }
    }, 'Crossover change rejected');
  }

  // ===== Input EQ / master toggles =====

  function setInputEqEnabled(enabled) {
    preset.value.inputEq.enabled = enabled;
    return push(() => apiClient.setEQEnabled(presetName.value, 'pref', enabled),
      'Failed to update EQ setting');
  }

  function saveInputEq(points) {
    const set = preset.value.inputEq.sets.find((s) => s.spl === 0);
    if (set) set.points = points;
    return push(() => apiClient.savePrefEqSet(presetName.value, points),
      'Failed to update EQ points');
  }

  function setDelaysEnabled(enabled) {
    preset.value.delaysEnabled = enabled;
    return push(() => apiClient.setSpeakerDelayEnabled(presetName.value, enabled),
      'Failed to update speaker delay setting');
  }

  function setFirEnabled(enabled) {
    preset.value.firEnabled = enabled;
    return push(() => apiClient.updateFIREnabled(presetName.value, enabled),
      'Failed to update FIR setting');
  }

  // ===== Websocket merge =====

  /**
   * Merge a live-update broadcast into the store. EQ point broadcasts are
   * deliberately not refetched: our own saves already updated local state,
   * and mid-drag refetches would fight the editor.
   */
  function handleLiveMessage(msg) {
    if (!preset.value || msg.presetName !== presetName.value) return;

    switch (msg.messageType) {
      case 'outputChanged':
        Object.assign(preset.value.outputs[msg.output], msg.changes);
        if (msg.firPool) preset.value.firPool = msg.firPool;
        if (msg.template) preset.value.template = msg.template;
        break;
      case 'crossoverChanged': {
        const point = preset.value.crossovers.find((x) => x.id === msg.id);
        if (point) point.freq = msg.crossoverFreq;
        break;
      }
      case 'crossoverEnabledChanged':
        for (const output of preset.value.outputs) {
          for (const which of ['hp', 'lp']) {
            const filter = output[which];
            if (filter.xover === msg.id && ['xover', 'off'].includes(filter.mode)) {
              filter.mode = msg.crossoverEnabled ? 'xover' : 'off';
            }
          }
        }
        break;
      case 'delayEnabledChanged':
        preset.value.delaysEnabled = msg.enabled;
        break;
      case 'firEnabledChanged':
        preset.value.firEnabled = msg.FIRFiltersEnabled;
        break;
      case 'eqEnabledChanged':
        preset.value.inputEq.enabled = msg.enabled;
        break;
    }
  }

  return {
    // state
    presetName, preset, isLoading, error, firFiles, templates,
    // getters
    outputs, enabledOutputs, crossovers, firPool, inputEqSets,
    isCrossoverEnabled, outputsReferencing,
    // actions
    loadPreset, refresh, loadFirFiles, loadTemplates, clearError,
    setOutputDelay, setOutputGain, setOutputFir, setOutputMute,
    setOutputInvert, setOutputEnabled, setOutputLabel, setOutputSource,
    setOutputFilter, saveOutputEq,
    setCrossoverFreq, setCrossoverEnabled,
    setInputEqEnabled, saveInputEq, setDelaysEnabled, setFirEnabled,
    handleLiveMessage,
  };
});
