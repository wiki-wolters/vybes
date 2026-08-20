/*
 * Device-wide state that more than one view needs:
 *  - dim (the firmware calls it mute): HomeView edits it, App.vue shows the
 *    "Dimmed" pill in the top bar.
 *  - the active preset's name: App.vue's nav links to its editor page.
 *  - master volume: stored per preset on the device, so it follows preset
 *    switches. HomeView edits the active preset's value; the preset editor
 *    edits whichever preset it has open (via the preset store).
 *
 * Seeded from GET /status and kept current by the muteChanged /
 * mutePercentChanged / volumeChanged / activePresetChanged broadcasts.
 *
 * The store holds state only — views keep making their own API calls so
 * failures surface in the banner they already own.
 */
import { defineStore } from 'pinia';
import { ref } from 'vue';
import apiClient from '../api-client.js';

export const useSystemStore = defineStore('system', () => {
  const dimmed = ref(false);
  const dimPercent = ref(100);
  const currentPreset = ref(null);
  const volume = ref(50);

  let unsubscribeLive = null;

  function applyStatus(status) {
    dimmed.value = Boolean(status?.mute?.muted);
    dimPercent.value = status?.mute?.percent ?? 100;
    currentPreset.value = status?.currentPreset ?? null;
    volume.value = status?.volume ?? 50;
  }

  function applyLiveMessage(data) {
    if (data?.messageType === 'muteChanged') dimmed.value = Boolean(data.muted);
    if (data?.messageType === 'mutePercentChanged') dimPercent.value = data.mutePercent;
    if (data?.messageType === 'activePresetChanged') {
      currentPreset.value = data.activePresetName ?? null;
      // The device carries the newly active preset's stored level, so the
      // master volume follows the switch without a refetch
      if (typeof data.volume === 'number') volume.value = data.volume;
    }
    // A volume edit on some other preset isn't the live master volume
    if (data?.messageType === 'volumeChanged' &&
        (!data.presetName || data.presetName === currentPreset.value)) {
      volume.value = data.volume;
    }
  }

  /** Seed from the device and follow it from then on. Idempotent. */
  async function connect() {
    if (!unsubscribeLive) unsubscribeLive = apiClient.connectLiveUpdates(applyLiveMessage);
    try {
      applyStatus(await apiClient.getStatus());
    } catch (error) {
      // Offline; App.vue's connectivity banner already says so
      console.warn('Could not read system state from /status:', error);
    }
  }

  function disconnect() {
    if (unsubscribeLive) {
      unsubscribeLive();
      unsubscribeLive = null;
    }
  }

  return {
    dimmed, dimPercent, currentPreset, volume,
    applyStatus, applyLiveMessage, connect, disconnect,
  };
});
