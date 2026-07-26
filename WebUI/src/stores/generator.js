/*
 * Signal generator state (tone + pink noise). The device keeps playing
 * whatever was started regardless of which page is open, so this state is
 * global: the GeneratorDock binds to it and stays visible from any page
 * while a generator runs.
 *
 * The device plays one generator signal at a time by convention — starting
 * one source stops the other first.
 *
 * Seeded from GET /status (tone/noise are non-zero only while running) and
 * kept current by the signal-generator websocket broadcasts, which carry
 * bare values with no messageType field.
 */
import { defineStore } from 'pinia';
import { ref, computed, watch } from 'vue';
import apiClient from '../api-client.js';

// Follow slider drags while running without flooding the device.
const UPDATE_INTERVAL_MS = 150;
// Our own PUTs come back as broadcasts; ignore them for this long so an
// echo of a stale value can't snap a slider back mid-drag.
const ECHO_SUPPRESS_MS = 1200;

export const useGeneratorStore = defineStore('generator', () => {
  const source = ref('noise'); // which source the UI controls: 'noise' | 'tone'
  const active = ref(null); // what the device is playing: null | 'noise' | 'tone'
  const toneFrequency = ref(1000);
  const toneVolume = ref(50);
  const noiseVolume = ref(50);
  const error = ref('');
  // Dock open/closed. Lives here so it survives route changes.
  const expanded = ref(false);
  // Rendered dock height in px (0 when hidden), measured by GeneratorDock.
  // App.vue pads the page bottom by this so content can scroll clear.
  const dockHeight = ref(0);

  const isActive = computed(() => active.value !== null);

  const statusLabel = computed(() => {
    if (active.value === 'noise') return 'Pink noise';
    if (active.value === 'tone') {
      const f = toneFrequency.value;
      const label = f >= 1000 ? `${parseFloat((f / 1000).toFixed(1))} kHz` : `${Math.round(f)} Hz`;
      return `Tone ${label}`;
    }
    return '';
  });

  // ===== Throttled follow-the-slider updates while running =====

  let updateTimer = null;
  let updatePending = false;
  let lastLocalSendAt = 0;

  function scheduleUpdate() {
    if (updateTimer) {
      updatePending = true;
      return;
    }
    sendUpdate();
    updateTimer = setTimeout(() => {
      updateTimer = null;
      if (updatePending) {
        updatePending = false;
        scheduleUpdate();
      }
    }, UPDATE_INTERVAL_MS);
  }

  async function sendUpdate() {
    lastLocalSendAt = Date.now();
    try {
      if (active.value === 'tone') {
        await apiClient.generateTone(Math.round(toneFrequency.value), Math.round(toneVolume.value));
      } else if (active.value === 'noise') {
        await apiClient.generateNoise(Math.round(noiseVolume.value));
      }
    } catch (err) {
      console.error('Generator update failed:', err);
    }
  }

  watch([toneFrequency, toneVolume], () => {
    if (active.value === 'tone') scheduleUpdate();
  });

  watch(noiseVolume, () => {
    if (active.value === 'noise') scheduleUpdate();
  });

  // ===== Start / stop / swap =====

  async function start(which = source.value) {
    error.value = '';
    source.value = which;
    try {
      // Both generators reach the Teensy mix through the "tone" input-gain
      // stage. The dock's volume slider is the only level control we expose,
      // so pin that stage to unity — a zeroed gain persisted in the device
      // config would otherwise make the generator silently inaudible.
      await apiClient.updateInputGains({ tone: 1 });
      // One signal at a time: silence the other source before starting.
      if (which === 'tone' && active.value === 'noise') await apiClient.generateNoise(0);
      if (which === 'noise' && active.value === 'tone') await apiClient.stopTone();
      lastLocalSendAt = Date.now();
      if (which === 'tone') {
        await apiClient.generateTone(Math.round(toneFrequency.value), Math.round(toneVolume.value));
      } else {
        await apiClient.generateNoise(Math.round(noiseVolume.value));
      }
      active.value = which;
    } catch (err) {
      active.value = null;
      error.value = `Failed to start ${which === 'tone' ? 'tone' : 'pink noise'}: ${err.message}`;
    }
  }

  async function stop() {
    error.value = '';
    const which = active.value;
    // Reflect the stop immediately — the user's intent is silence even if
    // the request fails, and the error message covers the failure case.
    active.value = null;
    try {
      lastLocalSendAt = Date.now();
      if (which === 'tone') await apiClient.stopTone();
      else if (which === 'noise') await apiClient.generateNoise(0);
    } catch (err) {
      error.value = `Failed to stop: ${err.message}. The generator may still be running on the device.`;
    }
  }

  /** Select a source; if a generator is running, swap the signal in place. */
  async function setSource(which) {
    if (which === source.value && active.value !== null) return;
    if (active.value !== null) {
      await start(which);
    } else {
      source.value = which;
    }
  }

  function toggle() {
    return isActive.value ? stop() : start();
  }

  // ===== Device state tracking =====

  function applyStatus(status) {
    const tone = status?.tone;
    const noise = status?.noise;
    // Non-zero values double as the "currently running" signal (stop zeroes
    // them), so only adopt them as slider positions when they're real.
    if (tone?.frequency >= 20) toneFrequency.value = tone.frequency;
    if (tone?.volume > 0) toneVolume.value = tone.volume;
    if (noise?.volume > 0) noiseVolume.value = noise.volume;
    if (tone?.volume > 0 && tone?.frequency >= 20) {
      active.value = 'tone';
      source.value = 'tone';
    } else if (noise?.volume > 0) {
      active.value = 'noise';
      source.value = 'noise';
    }
  }

  function applyLiveMessage(data) {
    if (!data || data.messageType) return; // generator broadcasts are bare values
    if (Date.now() - lastLocalSendAt < ECHO_SUPPRESS_MS) return; // our own echo
    if (typeof data.toneFrequency === 'number' && typeof data.toneVolume === 'number') {
      if (data.toneVolume > 0 && data.toneFrequency >= 20) {
        toneFrequency.value = data.toneFrequency;
        toneVolume.value = data.toneVolume;
        active.value = 'tone';
        source.value = 'tone';
      } else if (active.value === 'tone') {
        active.value = null;
      }
    } else if (typeof data.noiseVolume === 'number') {
      if (data.noiseVolume > 0) {
        noiseVolume.value = data.noiseVolume;
        active.value = 'noise';
        source.value = 'noise';
      } else if (active.value === 'noise') {
        active.value = null;
      }
    }
  }

  let unsubscribeLive = null;

  /** Seed from the device and follow it from then on. Idempotent. */
  async function connect() {
    if (!unsubscribeLive) unsubscribeLive = apiClient.connectLiveUpdates(applyLiveMessage);
    try {
      applyStatus(await apiClient.getStatus());
    } catch (err) {
      // Offline; App.vue's connectivity banner already says so
      console.warn('Could not read generator state from /status:', err);
    }
  }

  function disconnect() {
    if (unsubscribeLive) {
      unsubscribeLive();
      unsubscribeLive = null;
    }
    if (updateTimer) {
      clearTimeout(updateTimer);
      updateTimer = null;
      updatePending = false;
    }
  }

  return {
    source,
    active,
    isActive,
    statusLabel,
    toneFrequency,
    toneVolume,
    noiseVolume,
    error,
    expanded,
    dockHeight,
    start,
    stop,
    toggle,
    setSource,
    applyStatus,
    applyLiveMessage,
    connect,
    disconnect,
  };
});
