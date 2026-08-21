#include "SdRecorder.h"
#include "WavFormat.h"

// Patch-and-flush interval. The flush also syncs the FAT, which is what
// actually makes a crash-truncated recording playable.
static const unsigned long HEADER_PATCH_INTERVAL_MS = 5000;

// A queue this close to its 53-block ceiling almost certainly dropped
// blocks while loop() was stalled.
static const int OVERRUN_WATERMARK = 45;

// Next unused "rec-NNN.wav" in RECORDINGS_DIR. Three digits, so the picker
// sorts naturally; 999 recordings of anything is a full card anyway.
bool SdRecorder::allocateName() {
    int highest = 0;
    File dir = SD.open(RECORDINGS_DIR);
    if (dir && dir.isDirectory()) {
        File entry = dir.openNextFile();
        while (entry) {
            const char* n = entry.name();
            if (!entry.isDirectory() && strncmp(n, "rec-", 4) == 0) {
                int num = atoi(n + 4);
                if (num > highest) highest = num;
            }
            entry.close();
            entry = dir.openNextFile();
        }
    }
    if (dir) dir.close();
    if (highest >= 999) return false;
    snprintf(name, sizeof(name), "rec-%03d.wav", highest + 1);
    return true;
}

const char* SdRecorder::start() {
    if (recording) return nullptr; // already running: not an error

    if (!SD.exists(RECORDINGS_DIR) && !SD.mkdir(RECORDINGS_DIR)) {
        return "mkdir";
    }
    if (!allocateName()) {
        name[0] = '\0';
        return "full";
    }

    char path[48];
    snprintf(path, sizeof(path), RECORDINGS_DIR "/%s", name);
    file = SD.open(path, FILE_WRITE_BEGIN);
    if (!file) {
        name[0] = '\0';
        return "create";
    }

    uint8_t header[WavFormat::HEADER_BYTES];
    WavFormat::buildHeader(header, 0, (uint32_t)AUDIO_SAMPLE_RATE, 2, 16);
    if (file.write(header, sizeof(header)) != sizeof(header)) {
        file.close();
        SD.remove(path);
        name[0] = '\0';
        return "create";
    }

    framesWritten = 0;
    dataBytes = 0;
    lastPatchMs = millis();
    overrunReported = false;
    overrunPending = false;
    pendingError = nullptr;

    // Discard anything a previous session left queued, then start capturing
    qL.clear();
    qR.clear();
    qL.begin();
    qR.begin();
    recording = true;
    return nullptr;
}

void SdRecorder::patchHeader() {
    uint32_t pos = file.position();
    uint8_t field[4];
    WavFormat::writeU32(field, WavFormat::riffSizeField(dataBytes));
    file.seek(WavFormat::RIFF_SIZE_OFFSET);
    file.write(field, 4);
    WavFormat::writeU32(field, WavFormat::dataSizeField(dataBytes));
    file.seek(WavFormat::DATA_SIZE_OFFSET);
    file.write(field, 4);
    file.seek(pos);
    file.flush(); // sync the FAT so the file survives a crash from here back
}

// Interleave one matched L/R block pair and append it as a single 512-byte
// sector. Returns false when a pair isn't available or the write failed.
bool SdRecorder::drainOnePair() {
    if (qL.available() < 1 || qR.available() < 1) return false;

    int16_t interleaved[AUDIO_BLOCK_SAMPLES * 2]; // 512 bytes of loop stack
    const int16_t* l = qL.readBuffer();
    const int16_t* r = qR.readBuffer();
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        interleaved[2 * i] = l[i];
        interleaved[2 * i + 1] = r[i];
    }
    qL.freeBuffer();
    qR.freeBuffer();

    if (file.write((const uint8_t*)interleaved, sizeof(interleaved)) != sizeof(interleaved)) {
        pendingError = "write"; // card pulled or full: end the recording
        return false;
    }
    framesWritten += AUDIO_BLOCK_SAMPLES;
    dataBytes += sizeof(interleaved);
    return true;
}

void SdRecorder::service() {
    if (!recording) return;

    if (!overrunReported &&
        (qL.available() >= OVERRUN_WATERMARK || qR.available() >= OVERRUN_WATERMARK)) {
        overrunReported = true;
        overrunPending = true;
    }

    while (drainOnePair()) {}

    if (pendingError != nullptr) {
        stop(); // finalize what made it to the card
        return;
    }

    if (millis() - lastPatchMs >= HEADER_PATCH_INTERVAL_MS) {
        lastPatchMs = millis();
        patchHeader();
    }
}

void SdRecorder::stop() {
    if (!recording) return;
    qL.end();
    qR.end();
    // Final drain of whatever the queues still hold (paired; a stray
    // unmatched block is dropped by clear below)
    if (pendingError == nullptr) {
        while (drainOnePair()) {}
    }
    qL.clear();
    qR.clear();
    patchHeader();
    file.close();
    recording = false;
}

const char* SdRecorder::consumeError() {
    const char* e = pendingError;
    pendingError = nullptr;
    return e;
}

bool SdRecorder::consumeOverrunWarning() {
    if (!overrunPending) return false;
    overrunPending = false;
    return true;
}
