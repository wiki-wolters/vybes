#ifndef TEENSY_PROTOCOL_H
#define TEENSY_PROTOCOL_H

// Wire protocol shared with the Teensy: the command names, the outbound
// message size limit and the message builder. This header is pure C/C++
// (no Arduino or ESP-IDF dependencies) so the Teensy's host-native test
// suite can compile it and round-trip every command through the real
// Teensy-side parser. Keep it that way.

#include <stddef.h>
#include <string.h>

// Output channel commands (V1, docs/CHANNEL_ARCHITECTURE.md). Channels are
// 0-7; the ESP resolves crossover references to concrete frequencies before
// sending, so the Teensy only ever sees per-channel numbers.
//   setOutputGain   <ch> <dB>
//   setOutputMute   <ch> <0|1>
//   setOutputInvert <ch> <0|1>
//   setOutputSource <ch> <lGain> <rGain>
//   setOutputDelay  <ch> <us>
//   setOutputHp     <ch> <freq> <LR2|LR4|BW2>   (freq 0 = off)
//   setOutputLp     <ch> <freq> <LR2|LR4|BW2>
//   setOutputEq     <ch> <band> <freq> <q> <gain>
//   resetOutputEq   <ch> <fromBand>             (disables bands >= fromBand)
#define CMD_SET_OUTPUT_GAIN "setOutputGain"
#define CMD_SET_OUTPUT_MUTE "setOutputMute"
#define CMD_SET_OUTPUT_INVERT "setOutputInvert"
#define CMD_SET_OUTPUT_SOURCE "setOutputSource"
#define CMD_SET_OUTPUT_DELAY "setOutputDelay"
#define CMD_SET_OUTPUT_HP "setOutputHp"
#define CMD_SET_OUTPUT_LP "setOutputLp"
#define CMD_SET_OUTPUT_EQ "setOutputEq"
#define CMD_RESET_OUTPUT_EQ "resetOutputEq"

// Shared input EQ (L/R buses ahead of the routing matrix)
//   setInputEq        <band> <freq> <q> <gain>
//   resetInputEq      <fromBand>
//   setInputEqEnabled <0|1>
#define CMD_SET_INPUT_EQ "setInputEq"
#define CMD_RESET_INPUT_EQ "resetInputEq"
#define CMD_SET_INPUT_EQ_ENABLED "setInputEqEnabled"

// FIR Filter Commands. setFir is channel-indexed: "setFir <ch> <file>",
// bare "setFir <ch>" clears. setFirEnabled is preset-level.
#define CMD_SET_FIR "setFir"
#define CMD_SET_FIR_ENABLED "setFirEnabled"
#define CMD_LOAD_FIR_FILES "loadFirFiles"
#define CMD_GET_FILES "getFiles"

// Preset-level master delay toggle: setDelaysEnabled <0|1>
#define CMD_SET_DELAYS_ENABLED "setDelaysEnabled"

// Speaker and Gain Commands
#define CMD_SET_SPEAKER_GAINS "setSpeakerGains" // legacy remote/button path
#define CMD_SET_INPUT_GAINS "setInputGains"
#define CMD_SET_VOLUME "setVolume"

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
// Longest realistic message is "setFir <ch> <63-char filename>\n".
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
