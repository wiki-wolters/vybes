/*
 * Sweep (EQ tuning) mode. While enabled, the client sends "sweep:keepalive"
 * over the live socket; the ESP relays it as "setSweepMode 1" and the
 * Teensy floors its headroom pads at a fixed 12 dB reserve. The baseline
 * level then can't move while bands are edited: a boosted band swept across
 * the spectrum genuinely pokes out of an otherwise steady mix, like on a
 * mixing console. Turn the volume up to taste for the session - entering
 * the mode drops the overall level by the reserve.
 *
 * Keepalive-driven like the RTA: closing the tab ends the mode on its own.
 */
import { defineStore } from 'pinia';
import { ref } from 'vue';
import apiClient from '../api-client.js';

const KEEPALIVE_INTERVAL_MS = 2000;

export const useSweepStore = defineStore('sweep', () => {
  const enabled = ref(false);

  let keepaliveTimer = null;

  function setEnabled(value) {
    enabled.value = value;
    clearInterval(keepaliveTimer);
    keepaliveTimer = null;
    if (value) {
      apiClient.sendLiveMessage('sweep:keepalive');
      keepaliveTimer = setInterval(
        () => apiClient.sendLiveMessage('sweep:keepalive'),
        KEEPALIVE_INTERVAL_MS
      );
    } else {
      // Explicit off so the pads release now; the keepalive timeout is the
      // fallback for clients that vanish instead of toggling.
      apiClient.sendLiveMessage('sweep:off');
    }
  }

  function toggle() {
    setEnabled(!enabled.value);
  }

  return { enabled, setEnabled, toggle };
});
