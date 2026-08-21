#ifndef SD_RECORDER_H
#define SD_RECORDER_H

// Records the mixed stereo input to a WAV file on the SD card.
//
// The two AudioRecordQueue taps (owned by the sketch, wired to the L/R input
// mixers) buffer ~150ms of audio in the audio block pool; service() - called
// from loop() - drains them in matched pairs and appends interleaved 16-bit
// PCM in single-sector (512-byte) writes, so no staging buffer is needed and
// the recorder costs zero static RAM2 (the heap slack at a full FIR pool is
// ~15KB; see fir_filters.ino).
//
// The WAV header's running sizes are re-patched and flushed every few
// seconds, so a crash or power loss mid-recording costs at most those few
// seconds, not the file.
//
// All SD access happens in loop() context, like every other card user in
// this firmware.

#include <Arduino.h>
#include <Audio.h>
#include <SD.h>

#define RECORDINGS_DIR "/recordings"

class SdRecorder {
public:
    SdRecorder(AudioRecordQueue& left, AudioRecordQueue& right)
        : qL(left), qR(right) {}

    // Start a new recording under RECORDINGS_DIR with an auto-allocated
    // "rec-NNN.wav" name. Returns nullptr on success or a static error code:
    // mkdir, full, create.
    const char* start();

    // Finalize (drain, patch header, close). No-op when idle.
    void stop();

    // Drain the record queues to the card; call from loop().
    void service();

    bool isActive() const { return recording; }
    uint32_t seconds() const { return framesWritten / (uint32_t)AUDIO_SAMPLE_RATE; }
    const char* fileName() const { return name; }

    // One-shot events for the status reporter: a write failure ends the
    // recording and surfaces here; a queue overrun (loop() stalled past the
    // ~150ms the queues hold) reports once per recording.
    const char* consumeError();
    bool consumeOverrunWarning();

private:
    bool allocateName();
    void patchHeader();
    bool drainOnePair();

    AudioRecordQueue& qL;
    AudioRecordQueue& qR;
    File file;
    char name[32] = "";
    bool recording = false;
    uint32_t framesWritten = 0;
    uint32_t dataBytes = 0;
    unsigned long lastPatchMs = 0;
    bool overrunReported = false;
    bool overrunPending = false;
    const char* pendingError = nullptr;
};

#endif // SD_RECORDER_H
