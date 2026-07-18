#ifndef TEENSY_PROTOCOL_H
#define TEENSY_PROTOCOL_H

// Wire protocol shared with the Teensy: the command names, the outbound
// message size limit and the message builder. This header is pure C/C++
// (no Arduino or ESP-IDF dependencies) so the Teensy's host-native test
// suite can compile it and round-trip every command through the real
// Teensy-side parser. Keep it that way.

#include <stddef.h>
#include <string.h>

// Speaker and Gain Commands
#define CMD_SET_SPEAKER_GAINS "setSpeakerGains"
#define CMD_SET_INPUT_GAINS "setInputGains"
#define CMD_SET_VOLUME "setVolume"

// Crossover Commands
#define CMD_SET_CROSSOVER_FREQ "setCrossoverFrequency"
#define CMD_SET_CROSSOVER_ENABLED "setCrossoverEnabled"

// EQ Commands
#define CMD_SET_EQ_ENABLED "setEqEnabled"
#define CMD_SET_EQ_FILTER "setEq"
#define CMD_RESET_EQ_FILTERS "resetEqFilters"

// FIR Filter Commands
#define CMD_SET_FIR "setFir"
#define CMD_SET_FIR_ENABLED "setFirEnabled"
#define CMD_LOAD_FIR_FILES "loadFirFiles"
#define CMD_GET_FILES "getFiles"

// Delay Commands
#define CMD_SET_DELAYS "setDelays"
#define CMD_SET_DELAY_ENABLED "setDelayEnabled"

// Signal Generator Commands
#define CMD_SET_TONE "setTone"
#define CMD_STOP_TONE "stopTone"
#define CMD_SET_NOISE "setNoise"

// RTA (real-time analyzer) streaming: "setRta 1" starts/keeps-alive,
// "setRta 0" stops. The Teensy replies with "RTA <hex>" frames.
#define CMD_SET_RTA "setRta"

// System Commands
#define CMD_SET_MUTE "setMute"
#define CMD_SET_MUTE_PERCENT "setMutePercent"
#define CMD_PING "ping"

// Maximum length of a single message, including trailing newline and null.
// Longest realistic message is "setFir right <63-char filename>\n".
#define TEENSY_MSG_MAX 80

// strlcpy with BSD semantics (returns the length of src, i.e. the intended
// length), provided locally because it isn't part of standard C and the
// native test build may not have it.
static inline size_t teensyProtocolStrlcpy(char* dst, const char* src, size_t dstSize) {
    size_t srcLen = strlen(src);
    if (dstSize > 0) {
        size_t copyLen = (srcLen >= dstSize) ? dstSize - 1 : srcLen;
        memcpy(dst, src, copyLen);
        dst[copyLen] = '\0';
    }
    return srcLen;
}

// Build "<command> <p1> ... <p5>\n" into out (null parameters are skipped).
// Returns the message length (excluding the terminating null). A message
// that doesn't fit is truncated but stays newline-terminated; *truncated
// (when non-null) reports that so the caller can log it.
static inline size_t teensyBuildMessage(char* out, size_t outSize, const char* command,
                                        const char* p1, const char* p2, const char* p3,
                                        const char* p4, const char* p5, bool* truncated) {
    if (truncated) *truncated = false;
    size_t offset = teensyProtocolStrlcpy(out, command, outSize);
    if (offset >= outSize) offset = outSize - 1; // strlcpy reports intended length
    const char* params[5] = {p1, p2, p3, p4, p5};
    for (int i = 0; i < 5; i++) {
        if (!params[i]) continue;
        if (offset < outSize - 1) {
            out[offset++] = ' ';
            out[offset] = '\0';
        }
        offset += teensyProtocolStrlcpy(out + offset, params[i], outSize - offset);
        if (offset >= outSize) offset = outSize - 1; // strlcpy reports intended length
    }
    if (offset < outSize - 1) {
        out[offset++] = '\n';
        out[offset] = '\0';
    } else {
        if (truncated) *truncated = true;
        out[outSize - 2] = '\n';
        out[outSize - 1] = '\0';
        offset = outSize - 1;
    }
    return offset;
}

#endif // TEENSY_PROTOCOL_H
