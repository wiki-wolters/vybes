/*
 * Device-wide state that more than one view needs. Right now that's just
 * dim (the firmware calls it mute): HomeView edits it, App.vue shows the
 * "Dimmed" pill in the top bar. Seeded from GET /status and kept current by
 * the muteChanged / mutePercentChanged websocket broadcasts.
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

  let unsubscribeLive = null;

  function applyStatus(status) {
    dimmed.value = Boolean(status?.mute?.muted);
    dimPercent.value = status?.mute?.percent ?? 100;
  }

  function applyLiveMessage(data) {
    if (data?.messageType === 'muteChanged') dimmed.value = Boolean(data.muted);
    if (data?.messageType === 'mutePercentChanged') dimPercent.value = data.mutePercent;
  }

  /** Seed from the device and follow it from then on. Idempotent. */
  async function connect() {
    if (!unsubscribeLive) unsubscribeLive = apiClient.connectLiveUpdates(applyLiveMessage);
    try {
      applyStatus(await apiClient.getStatus());
    } catch (error) {
      // Offline; App.vue's connectivity banner already says so
      console.warn('Could not read dim state from /status:', error);
    }
  }

  function disconnect() {
    if (unsubscribeLive) {
      unsubscribeLive();
      unsubscribeLive = null;
    }
  }

  return { dimmed, dimPercent, applyStatus, applyLiveMessage, connect, disconnect };
});
