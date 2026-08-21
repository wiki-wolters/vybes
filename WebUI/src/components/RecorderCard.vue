<template>
  <!-- Only rendered while the device reports an SD card; the whole feature
       lives on the Teensy's card, so without one there is nothing to show -->
  <CardSection v-if="rec.sdPresent" title="Recorder">
    <p class="text-sm text-vybes-text-secondary mb-4">
      Records the mixed stereo input to the SD card. Playback runs through the
      active preset like any other source. Preset switching is locked while
      recording.
    </p>

    <!-- Record transport -->
    <div class="flex items-center gap-3 mb-4">
      <button
        v-if="!rec.recording.active"
        class="btn-primary flex items-center gap-2"
        @click="startRecording"
      >
        <span class="rec-dot bg-red-500"></span>Record
      </button>
      <template v-else>
        <button class="btn-danger flex items-center gap-2" @click="rec.stop()">
          Stop
        </button>
        <span class="flex items-center gap-2 text-sm text-vybes-text-primary tabular-nums">
          <span class="rec-dot bg-red-500 animate-pulse"></span>
          {{ rec.recording.file }} — {{ formatTime(rec.recording.seconds) }}
        </span>
      </template>
    </div>

    <p v-if="rec.error" class="text-sm text-red-400 mb-4 cursor-pointer" @click="rec.error = ''">
      {{ rec.error }}
    </p>

    <!-- Recordings list -->
    <div v-if="rec.files.length" class="divide-y divide-vybes-border">
      <div
        v-for="file in rec.files"
        :key="file.name"
        class="flex items-center gap-3 py-2"
      >
        <button
          class="btn-icon flex-none"
          :disabled="rec.recording.active"
          :title="isPlaying(file.name) ? 'Stop playback' : 'Play through the active preset'"
          :aria-label="isPlaying(file.name) ? `Stop ${file.name}` : `Play ${file.name}`"
          @click="togglePlay(file.name)"
        >
          <!-- stop square while this file plays, play triangle otherwise -->
          <svg v-if="isPlaying(file.name)" class="w-5 h-5 text-vybes-primary" fill="currentColor" viewBox="0 0 24 24">
            <rect x="7" y="7" width="10" height="10" rx="1" />
          </svg>
          <svg v-else class="w-5 h-5" :class="rec.recording.active ? 'text-vybes-text-secondary/40' : 'text-vybes-text-primary'" fill="currentColor" viewBox="0 0 24 24">
            <path d="M8 5.5v13l11-6.5z" />
          </svg>
        </button>

        <div class="flex-1 min-w-0">
          <p class="text-sm text-vybes-text-primary truncate">{{ file.name }}</p>
          <p class="text-xs text-vybes-text-secondary tabular-nums">
            <template v-if="isPlaying(file.name)">
              {{ formatTime(rec.playback.seconds) }} / {{ formatTime(rec.playback.length) }}
            </template>
            <template v-else>
              {{ formatTime(file.seconds) }} · {{ formatSize(file.size) }}
            </template>
          </p>
        </div>

        <button
          class="btn-icon flex-none"
          :disabled="rec.recording.active"
          title="Delete recording"
          :aria-label="`Delete ${file.name}`"
          @click="deleteFile(file.name)"
        >
          <svg class="w-5 h-5" :class="rec.recording.active ? 'text-vybes-text-secondary/40' : 'text-vybes-text-secondary hover:text-red-400'" fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" d="M6 7h12M9 7V5a1 1 0 011-1h4a1 1 0 011 1v2m-8 0l1 13h8l1-13" />
          </svg>
        </button>
      </div>
    </div>
    <p v-else class="text-sm text-vybes-text-secondary">No recordings yet.</p>
  </CardSection>
</template>

<script setup>
import { onMounted } from 'vue';
import CardSection from './shared/CardSection.vue';
import { useRecorderStore } from '../stores/recorder.js';

const rec = useRecorderStore();

onMounted(() => rec.connect());

function isPlaying(name) {
  return rec.playback.active && rec.playback.file === name;
}

async function startRecording() {
  try {
    await rec.start();
  } catch (error) {
    rec.error = `Could not start recording: ${error.message}`;
  }
}

async function togglePlay(name) {
  try {
    if (isPlaying(name)) await rec.stopPlayback();
    else await rec.play(name);
  } catch (error) {
    rec.error = `Playback failed: ${error.message}`;
  }
}

async function deleteFile(name) {
  if (!window.confirm(`Delete ${name}? This cannot be undone.`)) return;
  try {
    await rec.remove(name);
  } catch (error) {
    rec.error = `Delete failed: ${error.message}`;
  }
}

function formatTime(totalSeconds) {
  const s = Math.max(0, Math.floor(totalSeconds ?? 0));
  const mins = Math.floor(s / 60);
  const secs = s % 60;
  if (mins >= 60) {
    return `${Math.floor(mins / 60)}:${String(mins % 60).padStart(2, '0')}:${String(secs).padStart(2, '0')}`;
  }
  return `${mins}:${String(secs).padStart(2, '0')}`;
}

function formatSize(bytes) {
  if (!bytes) return '0 MB';
  const mb = bytes / (1024 * 1024);
  return mb >= 100 ? `${Math.round(mb)} MB` : `${mb.toFixed(1)} MB`;
}
</script>

<style scoped>
@reference '../style.css';

.rec-dot {
  @apply flex-none w-2.5 h-2.5 rounded-full;
}
</style>
