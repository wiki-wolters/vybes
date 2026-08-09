/*
 * Comparison mode: honest A/B level matching. While enabled, the client
 * sends "compare:keepalive" over the live socket every couple of seconds;
 * the ESP recomputes the active state's pink-weighted loudness on every
 * audible change (EQ/FIR toggles, preset switches - including ones made
 * from the device's button) and trims the louder state down to the
 * quietest state heard in the session, so louder can't win the A/B.
 * The applied trim comes back as a compareMode broadcast for the UI.
 *
 * Dropping the keepalives (tab closed, navigation away with the mode off)
 * ends the mode on its own: the ESP times out and clears the trim, and the
 * Teensy would time out even if the ESP vanished mid-session.
 */
import { defineStore } from 'pinia';
import { ref } from 'vue';
import apiClient from '../api-client.js';

const KEEPALIVE_INTERVAL_MS = 2000;

export const useCompareStore = defineStore('compare', () => {
  const enabled = ref(false);  // user intent (drives the keepalives)
  const active = ref(false);   // device-confirmed, from the broadcast
  const trimDb = ref(0);       // trim currently applied, <= 0

  let keepaliveTimer = null;
  let unsubscribeLive = null;

  function applyLiveMessage(data) {
    if (data?.messageType !== 'compareMode') return;
    active.value = Boolean(data.active);
    trimDb.value = Number(data.trimDb) || 0;
  }

  function setEnabled(value) {
    if (!unsubscribeLive) unsubscribeLive = apiClient.connectLiveUpdates(applyLiveMessage);
    enabled.value = value;
    clearInterval(keepaliveTimer);
    keepaliveTimer = null;
    if (value) {
      apiClient.sendLiveMessage('compare:keepalive');
      keepaliveTimer = setInterval(
        () => apiClient.sendLiveMessage('compare:keepalive'),
        KEEPALIVE_INTERVAL_MS
      );
    } else {
      // Explicit off so the trim clears now; the keepalive timeout is the
      // fallback for clients that vanish instead of toggling.
      apiClient.sendLiveMessage('compare:off');
    }
  }

  function toggle() {
    setEnabled(!enabled.value);
  }

  return { enabled, active, trimDb, setEnabled, toggle };
});
