#include "SdWavPlayer.h"
#include "WavFormat.h"

// Free ring slots. head==tail is empty, so one slot stays unused.
int SdWavPlayer::ringCountFree() const {
    int used = (int)ringTail - (int)ringHead;
    if (used < 0) used += RING_PAIRS;
    return RING_PAIRS - 1 - used;
}

// Release every queued block. Only call with playback stopped (update() no
// longer touches the ring once playing is false).
void SdWavPlayer::drainRing() {
    while (ringHead != ringTail) {
        uint8_t h = ringHead;
        release(ringL[h]);
        release(ringR[h]);
        ringHead = (uint8_t)((h + 1) % RING_PAIRS);
    }
}

// RIFF chunk walk: find fmt, validate it, stop at data. Standard writers put
// fmt ahead of data; anything else is rejected rather than guessed at.
bool SdWavPlayer::parseHeader(const char** errCode) {
    uint8_t hdr[12];
    if (file.read(hdr, 12) != 12 || memcmp(hdr, "RIFF", 4) != 0 ||
        memcmp(hdr + 8, "WAVE", 4) != 0) {
        *errCode = "format";
        return false;
    }

    WavFormat::Fmt fmt;
    bool haveFmt = false;
    while (true) {
        uint8_t chunk[8];
        if (file.read(chunk, 8) != 8) {
            *errCode = "format";
            return false;
        }
        uint32_t chunkLen = WavFormat::readU32(chunk + 4);
        if (memcmp(chunk, "fmt ", 4) == 0) {
            uint8_t body[16];
            if (chunkLen < 16 || file.read(body, 16) != 16 ||
                !WavFormat::parseFmtChunk(body, 16, fmt)) {
                *errCode = "format";
                return false;
            }
            haveFmt = true;
            if (chunkLen > 16 && !file.seek(file.position() + (chunkLen - 16))) {
                *errCode = "format";
                return false;
            }
        } else if (memcmp(chunk, "data", 4) == 0) {
            if (!haveFmt) {
                *errCode = "format";
                return false;
            }
            // Clip a lying header (e.g. a recording that crashed before its
            // final size patch... though those patch periodically) to the
            // bytes actually present.
            uint32_t avail = (uint32_t)(file.size() - file.position());
            dataRemaining = (chunkLen < avail) ? chunkLen : avail;
            break;
        } else {
            // Skip metadata chunks (LIST, fact, ...); chunks are word-aligned
            if (!file.seek(file.position() + chunkLen + (chunkLen & 1))) {
                *errCode = "format";
                return false;
            }
        }
    }

    if (fmt.format != 1 || fmt.channels != 2 || fmt.bitsPerSample != 16 ||
        fmt.sampleRate != (uint32_t)AUDIO_SAMPLE_RATE) {
        *errCode = "format";
        return false;
    }
    framesTotal = dataRemaining / 4;
    return true;
}

// Read one 128-frame stereo chunk from the file into a fresh block pair and
// queue it. Returns false when the ring is full, allocation fails, or the
// file is exhausted (which also marks eofQueued).
bool SdWavPlayer::queueOneBlockPair() {
    if (ringCountFree() == 0 || dataRemaining == 0) return false;

    uint8_t raw[AUDIO_BLOCK_SAMPLES * 4]; // 128 frames x 2ch x 16-bit
    uint32_t want = sizeof(raw);
    if (want > dataRemaining) want = dataRemaining;
    int got = file.read(raw, want);
    if (got <= 0) {
        // Read failure (card pulled, truncated file): end playback cleanly
        dataRemaining = 0;
        eofQueued = true;
        return false;
    }
    dataRemaining -= (uint32_t)got;

    audio_block_t* left = allocate();
    if (left == nullptr) return false;
    audio_block_t* right = allocate();
    if (right == nullptr) {
        release(left);
        // Rewind so the frames aren't lost - the pool should free up within
        // a few update cycles.
        file.seek(file.position() - got);
        dataRemaining += (uint32_t)got;
        return false;
    }

    const int16_t* in = (const int16_t*)raw;
    int frames = got / 4;
    for (int i = 0; i < frames; i++) {
        left->data[i] = in[2 * i];
        right->data[i] = in[2 * i + 1];
    }
    // Zero-pad a short final chunk
    for (int i = frames; i < AUDIO_BLOCK_SAMPLES; i++) {
        left->data[i] = 0;
        right->data[i] = 0;
    }

    uint8_t t = ringTail;
    ringL[t] = left;
    ringR[t] = right;
    ringTail = (uint8_t)((t + 1) % RING_PAIRS);

    if (dataRemaining == 0) eofQueued = true;
    return true;
}

bool SdWavPlayer::play(const char* path, const char** errCode) {
    stop();

    file = SD.open(path);
    if (!file) {
        *errCode = "notfound";
        return false;
    }
    if (!parseHeader(errCode)) {
        file.close();
        return false;
    }

    // Keep just the basename for status lines
    const char* base = strrchr(path, '/');
    strlcpy(name, base ? base + 1 : path, sizeof(name));

    eofQueued = (dataRemaining == 0);
    finished = false;
    finishedEventPending = false;
    pendingClose = true; // service() closes the file when playback ends
    underruns = 0;
    while (queueOneBlockPair()) {} // pre-fill

    AudioNoInterrupts();
    framesPlayed = 0;
    playing = true;
    AudioInterrupts();
    return true;
}

void SdWavPlayer::stop() {
    if (playing) {
        AudioNoInterrupts();
        playing = false;
        AudioInterrupts();
    }
    drainRing();
    eofQueued = false;
    finished = false;
    if (file) file.close();
    pendingClose = false;
    name[0] = '\0';
}

void SdWavPlayer::service() {
    if (finished) {
        // Ran to the end: update() already stopped consuming
        drainRing();
        if (file) file.close();
        pendingClose = false;
        finished = false;
        playing = false;
        finishedEventPending = true;
        name[0] = '\0';
        return;
    }
    if (!playing) return;
    while (queueOneBlockPair()) {}
}

bool SdWavPlayer::consumeFinishedEvent() {
    if (!finishedEventPending) return false;
    finishedEventPending = false;
    return true;
}

void SdWavPlayer::update() {
    if (!playing) return;
    uint8_t h = ringHead;
    if (h == ringTail) {
        if (eofQueued) {
            // Drained the final block: end playback. Loop-side cleanup
            // (closing the file) happens in service().
            playing = false;
            finished = true;
        } else {
            underruns++; // service() will catch up; output stays silent
        }
        return;
    }
    transmit(ringL[h], 0);
    transmit(ringR[h], 1);
    release(ringL[h]);
    release(ringR[h]);
    ringHead = (uint8_t)((h + 1) % RING_PAIRS);
    framesPlayed += AUDIO_BLOCK_SAMPLES;
}
