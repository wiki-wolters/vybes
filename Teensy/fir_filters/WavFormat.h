#ifndef WAV_FORMAT_H
#define WAV_FORMAT_H

// Canonical 44-byte PCM WAV header: build, patch and field helpers for the
// SD recorder (SdRecorder) and player (SdWavPlayer). Pure C++ with no
// Arduino dependencies so the host-native test suite can cover it - the
// header a crashed recording leaves behind is only as good as this code.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace WavFormat {

static const size_t HEADER_BYTES = 44;

// Byte offsets of the two running sizes the recorder patches while writing,
// so a crash mid-recording loses seconds, not the whole file.
static const size_t RIFF_SIZE_OFFSET = 4;
static const size_t DATA_SIZE_OFFSET = 40;

static inline void writeU16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static inline void writeU32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static inline uint16_t readU16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t readU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Fill out[HEADER_BYTES] with a canonical PCM header (fmt chunk directly
// followed by the data chunk - the layout every player accepts).
static inline void buildHeader(uint8_t* out, uint32_t dataBytes,
                               uint32_t sampleRate, uint16_t channels,
                               uint16_t bitsPerSample) {
    const uint16_t blockAlign = (uint16_t)(channels * (bitsPerSample / 8));
    memcpy(out, "RIFF", 4);
    writeU32(out + 4, 36 + dataBytes);
    memcpy(out + 8, "WAVE", 4);
    memcpy(out + 12, "fmt ", 4);
    writeU32(out + 16, 16);            // fmt chunk size
    writeU16(out + 20, 1);             // PCM
    writeU16(out + 22, channels);
    writeU32(out + 24, sampleRate);
    writeU32(out + 28, sampleRate * blockAlign);
    writeU16(out + 32, blockAlign);
    writeU16(out + 34, bitsPerSample);
    memcpy(out + 36, "data", 4);
    writeU32(out + 40, dataBytes);
}

// The two 4-byte values a size patch rewrites in place.
static inline uint32_t riffSizeField(uint32_t dataBytes) { return 36 + dataBytes; }
static inline uint32_t dataSizeField(uint32_t dataBytes) { return dataBytes; }

// One parsed fmt chunk (the fields the player validates).
struct Fmt {
    uint16_t format = 0;       // 1 = PCM
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
};

// Parse a fmt chunk body (at least 16 bytes). Returns false on a short body.
static inline bool parseFmtChunk(const uint8_t* body, uint32_t bodyLen, Fmt& out) {
    if (bodyLen < 16) return false;
    out.format = readU16(body + 0);
    out.channels = readU16(body + 2);
    out.sampleRate = readU32(body + 4);
    out.bitsPerSample = readU16(body + 14);
    return true;
}

// Playback duration of a PCM data chunk in whole seconds (rounded down).
static inline uint32_t dataSeconds(uint32_t dataBytes, uint32_t sampleRate,
                                   uint16_t channels, uint16_t bitsPerSample) {
    const uint32_t bytesPerSecond = sampleRate * channels * (bitsPerSample / 8);
    if (bytesPerSecond == 0) return 0;
    return dataBytes / bytesPerSecond;
}

} // namespace WavFormat

#endif // WAV_FORMAT_H
