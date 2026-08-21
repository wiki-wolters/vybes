/*
 * SD recorder/player state, mirrored from the device:
 *  - HomeView's recorder card records the stereo input and plays recordings.
 *  - App.vue shows the "Recording" pill in the top bar from any page.
 *  - HomeView locks the preset buttons while recording (a preset switch's
 *    FIR load on the Teensy would glitch the recording; the device refuses
 *    it with 409 regardless — this store just makes the lock visible).
 *
 * Seeded from GET /recorder and kept current by the recorderState /
 * recordingsChanged / recorderError / recorderWarning broadcasts. Commands
 * are asynchronous: the device acknowledges immediately and the new state
 * arrives over the websocket.
 */
import { defineStore } from 'pinia';
import { ref, computed } from 'vue';
import apiClient from '../api-client.js';

const ERROR_TEXT = {
  nosd: 'No SD card in the device',
  busy: 'The recorder is busy',
  badname: 'Invalid recording name',
  mkdir: 'Could not create the recordings folder on the SD card',
  full: 'Too many recordings — delete some first',
  create: 'Could not create the recording file',
  write: 'SD write failed — recording stopped (card full or removed?)',
  notfound: 'Recording not found on the SD card',
  format: 'Not a playable recording (16-bit 44.1kHz stereo WAV)',
  delete: 'Could not delete the recording',
};

export const useRecorderStore = defineStore('recorder', () => {
  const sdPresent = ref(false);
  const recording = ref({ active: false, file: '', seconds: 0 });
  const playback = ref({ active: false, file: '', seconds: 0, length: 0 });
  const files = ref([]);
  const error = ref('');

  const isRecording = computed(() => recording.value.active);

  let unsubscribeLive = null;

  async function refresh() {
    try {
      const state = await apiClient.getRecorder();
      sdPresent.value = Boolean(state?.sdPresent);
      if (state?.recording) recording.value = state.recording;
      if (state?.playback) playback.value = state.playback;
      files.value = state?.files ?? [];
    } catch (e) {
      // Offline; App.vue's connectivity banner already says so
      console.warn('Could not read recorder state from /recorder:', e);
    }
  }

  function applyLiveMessage(data) {
    if (data?.messageType === 'recorderState') {
      sdPresent.value = Boolean(data.sdPresent);
      if (data.recording) recording.value = data.recording;
      if (data.playback) playback.value = data.playback;
    }
    if (data?.messageType === 'recordingsChanged') refresh();
    if (data?.messageType === 'recorderError') {
      error.value = ERROR_TEXT[data.code] ?? `Recorder error: ${data.code}`;
    }
    if (data?.messageType === 'recorderWarning' && data.detail === 'overrun') {
      error.value = 'The device stalled briefly — the recording may have a short gap';
    }
  }

  async function start() {
    error.value = '';
    await apiClient.startRecording();
  }

  async function stop() {
    await apiClient.stopRecording();
  }

  async function play(name) {
    error.value = '';
    await apiClient.playRecording(name);
  }

  async function stopPlayback() {
    await apiClient.stopRecordingPlayback();
  }

  async function remove(name) {
    error.value = '';
    await apiClient.deleteRecording(name);
  }

  /** Seed from the device and follow it from then on. Idempotent. */
  async function connect() {
    if (!unsubscribeLive) unsubscribeLive = apiClient.connectLiveUpdates(applyLiveMessage);
    await refresh();
  }

  function disconnect() {
    if (unsubscribeLive) {
      unsubscribeLive();
      unsubscribeLive = null;
    }
  }

  return {
    sdPresent, recording, playback, files, error, isRecording,
    refresh, applyLiveMessage, start, stop, play, stopPlayback, remove,
    connect, disconnect,
  };
});
