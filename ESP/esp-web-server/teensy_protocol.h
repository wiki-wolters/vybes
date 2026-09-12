#ifndef TEENSY_PROTOCOL_H
#define TEENSY_PROTOCOL_H

// Wire protocol shared with the Teensy: the command names, the outbound
// message size limit and the message builder. This header is pure C/C++
// (no Arduino or ESP-IDF dependencies) so the Teensy's host-native test
// suite can compile it and round-trip every command through the real
// Teensy-side parser. Keep it that way.

#include <stddef.h>
#include <stdint.h>
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

// Per-output PEQ bypass: "setOutputEqEnabled <ch> <0|1>". Non-destructive -
// the stored bands stay; the Teensy's shared output pad recomputes so only
// live boosts cost headroom.
#define CMD_SET_OUTPUT_EQ_ENABLED "setOutputEqEnabled"

// Audio hold, bracketing a full state sync: "setConfigHold <0|1>".
// A sync is hundreds of commands, and every value in flight is a default
// until its command lands - master volume, per-output gain, input gains and
// crossovers all arrive at different moments. With the hold asserted the
// Teensy keeps every output amp at zero, so a partially applied config is
// never audible. The Teensy also holds from power-on until the first sync
// releases it, and holds across FIR loads on its own account.
#define CMD_SET_CONFIG_HOLD "setConfigHold"

// Shared input EQ (L/R buses ahead of the routing matrix)
//   setInputEq        <band> <freq> <q> <gain>
//   resetInputEq      <fromBand>
//   setInputEqEnabled <0|1>
#define CMD_SET_INPUT_EQ "setInputEq"
#define CMD_RESET_INPUT_EQ "resetInputEq"
#define CMD_SET_INPUT_EQ_ENABLED "setInputEqEnabled"

// Dynamic EQ (docs/DYNAMIC_EQ.md): the input EQ has two volume anchors that
// share band frequencies and Qs and differ only in gain. Above the reference
// anchor the Teensy interpolates toward the loud gains and holds beyond it;
// below it, setLoudness turns on the ISO 226-derived bass compensation.
//   setInputEqLoudGain <band> <gain>   # loud-anchor gain for one band
//   setInputEqAnchors  <refPct> <loudPct>  # 0-100; loud 0 = no loud anchor
//   setLoudness        <0|1>
#define CMD_SET_INPUT_EQ_LOUD_GAIN "setInputEqLoudGain"
#define CMD_SET_INPUT_EQ_ANCHORS "setInputEqAnchors"
#define CMD_SET_LOUDNESS "setLoudness"

// FIR Filter Commands. setFir is channel-indexed: "setFir <ch> <file>",
// bare "setFir <ch>" clears. setFirEnabled is preset-level.
//
// The shared tap pool is charged per file in whole partitions of this many
// taps, because that is what the memory actually costs: the Teensy's fast-
// convolution engine rounds every filter up to whole 128-tap partitions,
// and its coefficient arena is statically sized as FIR_TAP_POOL /
// FIR_POOL_CHARGE_QUANTUM partitions. Charging raw tap counts would admit
// sets (many odd-length files) that overflow that arena. Both the ESP's
// accounting (api_fir.cpp) and the Teensy's load-time check charge this
// way; partition-aligned files (any multiple of 128 taps) are unaffected.
#define FIR_POOL_CHARGE_QUANTUM 128
#define CMD_SET_FIR "setFir"
#define CMD_SET_FIR_ENABLED "setFirEnabled"
#define CMD_LOAD_FIR_FILES "loadFirFiles"
#define CMD_GET_FILES "getFiles"

// FIR file upload/delete (docs/AUTO_FIR_CONTRACTS.md, Slice A). Raw file
// bytes stream from the browser through the ESP to the Teensy's SD card in
// base64-encoded lines, so no side ever needs to buffer the whole file.
//
//   firPutBegin <name> <size> <crc32>  -> FIRPUT BEGIN <name>
//                                      |  FIRPUT ERR <reason>
//   firPut <seq> <base64>              -> FIRPUT ACK <seq>   (every
//                                         FIR_PUT_ACK_STRIDE'th line and the
//                                         final line)
//                                      |  FIRPUT ERR <reason> (aborts)
//   firPutEnd                          -> FIRPUT OK <name> <size> <taps>
//                                      |  FIRPUT ERR <reason>
//   firPutAbort                        -> FIRPUT STOP
//   firDelete <name>                   -> FIRDEL OK <name>
//                                      |  FIRDEL ERR <reason>
//
// seq counts from 0, decimal. base64 payload is <= FIR_PUT_B64_MAX chars
// (FIR_PUT_CHUNK_BYTES raw bytes), so a full-pool file is ~1,100 lines. Flow
// control: the ESP sends at most FIR_PUT_ACK_STRIDE lines beyond the last
// ACK'd seq. crc32 is IEEE 802.3 (reflected, init/final XOR 0xFFFFFFFF),
// 8 lowercase hex chars, over the raw file bytes.
//
// ERR reasons (single tokens): badName, badSize (bad/mismatched size, incl.
// a non-multiple-of-4 .bin or a firPut that would overrun the claimed size),
// badSeq (gap or repeat), badB64 (malformed or oversize payload), crc
// (mismatch, or a malformed crc32 argument), sd, noSpace, busy (recording or
// another transfer active), state (firPut/firPutEnd without a live Begin).
// firDelete adds notfound (no such file - reusing the recorder's token
// above, not part of the firPut vocabulary).
//
// The Teensy writes FIR_UPLOAD_TMP_NAME on the SD, and on firPutEnd verifies
// byte count and CRC, removes any existing target, renames, then replies. A
// failed verify leaves the SD exactly as it was (temp file removed).
#define CMD_FIR_PUT_BEGIN "firPutBegin"
#define CMD_FIR_PUT "firPut"
#define CMD_FIR_PUT_END "firPutEnd"
#define CMD_FIR_PUT_ABORT "firPutAbort"
#define CMD_FIR_DELETE "firDelete"

#define FIR_UPLOAD_MIN_SIZE 4
#define FIR_UPLOAD_MAX_SIZE 51200
#define FIR_PUT_CHUNK_BYTES 45   // raw bytes per firPut line (3-byte aligned)
#define FIR_PUT_B64_MAX 60       // 4/3 * FIR_PUT_CHUNK_BYTES
#define FIR_PUT_ACK_STRIDE 16    // ack cadence, and the ESP's flow-control window
// Longest "firPut <seq> <base64>\n" line the ESP can put on the wire:
// command + space + seq digits + space + payload + newline. The largest
// upload is FIR_UPLOAD_MAX_SIZE / FIR_PUT_CHUNK_BYTES ~= 1,138 lines, so 6
// digits of seq is generous headroom.
#define FIR_PUT_LINE_MAX (6 + 1 + 6 + 1 + FIR_PUT_B64_MAX + 1)
// The flow-control window above lets the ESP put FIR_PUT_ACK_STRIDE lines on
// the wire before it waits for an ACK, so the Teensy's Serial1 RX buffer has
// to be able to hold that much. Undersize it and a burst overruns the buffer,
// a line is lost, and the transfer dies with badSeq - which is exactly what a
// 512-byte buffer did to every kernel over ~1,500 taps. fir_filters.ino
// static_asserts espRxBuffer against this.
#define FIR_PUT_MAX_IN_FLIGHT_BYTES (FIR_PUT_ACK_STRIDE * FIR_PUT_LINE_MAX)
#define FIR_UPLOAD_TMP_NAME "upload.tmp"

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

// Input-bus level meter streaming: same keepalive scheme as setRta. The
// Teensy replies with "VU llrrf" frames at 20Hz - one hex byte per channel
// mapping peak dBFS -60..0 onto 0..255, plus a flag digit (bit0/bit1 =
// left/right clipped: a flat-topped run of full-scale samples).
#define CMD_SET_VU "setVu"

// SD playback level into the input mix (aux input 2): "setPlaybackGain
// <0..1>". Its own command because the message builder carries at most
// five parameters and setInputGains already uses all five.
#define CMD_SET_PLAYBACK_GAIN "setPlaybackGain"

// Mixed-input multiband compressor (3 bands: 0 bass, 1 mid/voice, 2 treble)
//   setCompEnabled       <0|1>
//   setCompXover         <f1> <f2>
//   setCompBand          <band> <thresholdDb ratio attackMs releaseMs makeupDb>
//                        (five values space-packed into one builder param)
//   setCompBandBypass    <band> <0|1>
//   setCompSolo          <band, -1 clears>
//   setCompStrength      <0-100>
//   setCompVoicePriority <dB>
#define CMD_SET_COMP_ENABLED "setCompEnabled"
#define CMD_SET_COMP_XOVER "setCompXover"
#define CMD_SET_COMP_BAND "setCompBand"
#define CMD_SET_COMP_BAND_BYPASS "setCompBandBypass"
#define CMD_SET_COMP_SOLO "setCompSolo"
#define CMD_SET_COMP_STRENGTH "setCompStrength"
#define CMD_SET_COMP_VOICE_PRIORITY "setCompVoicePriority"

// GRM (compressor gain-reduction meter) streaming: same keepalive scheme as
// setRta. The Teensy replies with "GRM <6 hex>" frames.
#define CMD_SET_GRM "setGrm"

// Auto delay alignment probe.
//   startDelayProbe <mask> <level>   mask = decimal 8-bit bitmap, bit n =
//                                    output n (the ESP sets only enabled
//                                    outputs); level = 0-100 (%)
//   stopDelayProbe
// The Teensy plays one log chirp per masked output - outputs ascending,
// then the same list reversed (the UI averages the two passes to cancel
// phone-clock drift) - at exact sample offsets PROBE_PRE_ROLL_SAMPLES +
// k * PROBE_SPACING_SAMPLES on its own audio clock, soloing one output per
// chirp. The UI owns mutual exclusion: no preset switches or FIR loads
// while a probe runs. Reply lines (relayed to the web UI as probeEvent):
//   PROBE START <mask> <nChirps> <preRoll> <spacing> <chirpLen>
//   PROBE CHIRP <slot> <ch>
//   PROBE WARN unrouted <ch>
//   PROBE DONE               (sequence complete, state restored)
//   PROBE STOP               (stopped by command)
//   PROBE ERR emptyMask | PROBE ERR aborted firLoad
#define CMD_START_DELAY_PROBE "startDelayProbe"
#define CMD_STOP_DELAY_PROBE "stopDelayProbe"

// Output solo for per-output EQ measurement: "soloOutput <ch>" silences
// every other output; same keepalive scheme as setRta (the ESP refreshes it
// while a web client holds a channel soloed, the Teensy times out on its
// own). -1 or any out-of-range channel clears the solo immediately. The
// soloed output keeps its normal gain/volume product - the web UI measures
// the audible reality, so nothing is forced to a probe level.
#define CMD_SOLO_OUTPUT "soloOutput"

// Probe chirp/schedule contract, shared by ProbeSource (Teensy), the
// /probe/delay API (ESP) and delay-align.js (web UI reference generator).
// Chirp k starts at sample PROBE_PRE_ROLL_SAMPLES + k * PROBE_SPACING_SAMPLES.
// The spacing leaves a ~743ms gap so the output amps' 60ms-tau solo ramp
// (~342ms to fully settle) finishes well before the next chirp.
#define PROBE_SAMPLE_RATE 44100
#define PROBE_PRE_ROLL_SAMPLES 65536  /* 1486ms before the first chirp */
#define PROBE_SPACING_SAMPLES 49152   /* 1114.6ms chirp-start to chirp-start */
#define PROBE_CHIRP_SAMPLES 16384     /* 371.5ms log sweep */
#define PROBE_TAIL_SAMPLES 8192       /* silence after the last chirp */
#define PROBE_FADE_SAMPLES 512        /* raised-cosine fade each end */
#define PROBE_F0_HZ 60.0
#define PROBE_F1_HZ 8000.0

// Measurement sweep probe (docs/AUTO_FIR_CONTRACTS.md). Reuses the delay
// probe's waveform/soloing/keepalive machinery, parameterized at runtime
// instead of the PROBE_* constants above (which the delay probe keeps using
// unchanged):
//
//   startSweepProbe <mask> <level%> <f0> <f1> <chirpSamples> <nPasses>
//     -> SWEEP START <mask> <nPasses> <preRoll> <spacing> <chirpSamples>
//                    <f0> <f1> <fade>
//     |  SWEEP CHIRP <slot> <ch>
//     |  SWEEP WARN unrouted <ch>
//     |  SWEEP DONE | SWEEP STOP | SWEEP ERR <reason>
//
// There is no separate stop command: stopDelayProbe stops whichever kind of
// probe (delay or sweep) is currently active, since both share the one
// underlying chirp sequencer and are mutually exclusive.
//
// Slot order: masked outputs ascending, then the same list repeated
// nPasses times - NOT reversed like the delay probe, since sweep passes are
// a pass-to-pass drift/consistency check on the SAME output rather than a
// drift-cancelling forward/reverse pair.
//
// spacing = chirpSamples + SWEEP_MIN_TAIL_SAMPLES (>= 1.5s IR tail at
// device rate). The values echoed in SWEEP START are the single source of
// truth for the browser's reference generator - it never assumes compiled-in
// constants. ERR reasons: emptyMask, badParam (f0/f1/chirpSamples/nPasses
// out of the ranges DelayProbe.cpp enforces), aborted firLoad (a FIR load
// interrupted the sequence, like the delay probe).
#define CMD_START_SWEEP_PROBE "startSweepProbe"
#define SWEEP_MIN_TAIL_SAMPLES 66150UL
#define SWEEP_MAX_PASSES 16
#define SWEEP_DEFAULT_F0_HZ 20.0
#define SWEEP_DEFAULT_F1_HZ 20000.0
#define SWEEP_DEFAULT_CHIRP_SAMPLES 131072UL
#define SWEEP_DEFAULT_N_PASSES 2

// SD recorder / player. Recordings live in /recordings on the Teensy's SD
// card as 16-bit 44.1kHz stereo WAVs named rec-NNN.wav; filenames on the
// wire are bare names (no paths). Only available while a card is present,
// and never both directions at once (the handlers enforce it).
//   startRecording               starts a new auto-named recording
//   stopRecording                finalizes and closes it
//   getRecordings                replies "RECFILES <sd 0|1>", one
//                                "name bytes seconds" line per file, "EOT"
//   playRecording <name>         plays through the input chain (aux input 2)
//   stopPlayback
//   deleteRecording <name>
// Unsolicited status lines (also sent in reply to the commands above):
//   REC STATE <sd> <rec> <recFile|-> <recSecs> <play> <playFile|-> <pos> <len>
//     - on every change, which includes a 1Hz position tick while active
//   REC ERR <code> <file|->      nosd, busy, badname, mkdir, full, create,
//                                write, notfound, format, delete
//   REC WARN <what>              overrun (loop stalled past the ~150ms the
//                                record queues buffer), stopped firload
// A fresh RECFILES list follows any change to the set of recordings.
#define CMD_START_RECORDING "startRecording"
#define CMD_STOP_RECORDING "stopRecording"
#define CMD_GET_RECORDINGS "getRecordings"
#define CMD_PLAY_RECORDING "playRecording"
#define CMD_STOP_PLAYBACK "stopPlayback"
#define CMD_DELETE_RECORDING "deleteRecording"

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

// Build "<command> <p1> ... <p6>\n" into out (null parameters are skipped).
// Returns the message length (excluding the terminating null). A message
// that doesn't fit is truncated but stays newline-terminated; *truncated
// (when non-null) reports that so the caller can log it.
static inline size_t teensyBuildMessage(char* out, size_t outSize, const char* command,
                                        const char* p1, const char* p2, const char* p3,
                                        const char* p4, const char* p5, const char* p6,
                                        bool* truncated) {
    if (truncated) *truncated = false;
    size_t offset = teensyProtocolStrlcpy(out, command, outSize);
    if (offset >= outSize) offset = outSize - 1; // strlcpy reports intended length
    const char* params[6] = {p1, p2, p3, p4, p5, p6};
    for (int i = 0; i < 6; i++) {
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

// --- CRC32 (IEEE 802.3: reflected, init/final XOR 0xFFFFFFFF) ---
// Used by the FIR upload path (docs/AUTO_FIR_CONTRACTS.md) to verify a
// transferred file. Implemented once here so the ESP (encoder) and the
// Teensy (verifier) can never drift apart, and so the native test suite
// exercises the exact bytes both firmwares run.

// Feed raw bytes through a running CRC32 accumulator. Seed with
// crc32Init() and finish with crc32Finish() to get the standard value.
static inline uint32_t crc32Init(void) { return 0xFFFFFFFFu; }

static inline uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint32_t)data[i];
        for (int bit = 0; bit < 8; bit++) {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

static inline uint32_t crc32Finish(uint32_t crc) { return crc ^ 0xFFFFFFFFu; }

// One-shot helper over a whole buffer.
static inline uint32_t crc32Of(const uint8_t* data, size_t len) {
    return crc32Finish(crc32Update(crc32Init(), data, len));
}

// 8 lowercase hex chars + a terminating null; out must be >= 9 bytes.
static inline void crc32ToHex(uint32_t crc, char* out) {
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        out[i] = digits[(crc >> (28 - i * 4)) & 0xFu];
    }
    out[8] = '\0';
}

// Strict parse of exactly 8 lowercase hex chars (nothing more, nothing
// less - uppercase or any other length is rejected, matching the wire
// format). Returns false without touching *out on malformed input.
static inline bool crc32FromHex(const char* s, uint32_t* out) {
    if (s == NULL || strlen(s) != 8) return false;
    uint32_t v = 0;
    for (int i = 0; i < 8; i++) {
        char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else return false;
        v = (v << 4) | (uint32_t)d;
    }
    *out = v;
    return true;
}

// --- base64 (standard alphabet, '=' padding) ---
// Used to carry raw FIR bytes over the line-based UART protocol. Shared here
// (rather than reusing e.g. the ESP32 Arduino core's encode-only `base64`
// class) so the encoder and decoder are provably the same algorithm on both
// firmwares, and so the native test suite can round-trip real payloads.

static inline int base64DecodeChar(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// Encodes len bytes of data into out (which must be able to hold at least
// base64EncodedLength(len) + 1 bytes, including the terminating null).
// Returns the encoded length, or 0 if it wouldn't fit in outCap.
static inline size_t base64EncodedLength(size_t len) {
    return ((len + 2) / 3) * 4;
}

static inline size_t base64Encode(const uint8_t* data, size_t len, char* out, size_t outCap) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t needed = base64EncodedLength(len);
    if (needed + 1 > outCap) return 0;
    size_t o = 0, i = 0;
    while (i + 3 <= len) {
        uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | data[i + 2];
        out[o++] = table[(n >> 18) & 0x3F];
        out[o++] = table[(n >> 12) & 0x3F];
        out[o++] = table[(n >> 6) & 0x3F];
        out[o++] = table[n & 0x3F];
        i += 3;
    }
    size_t rem = len - i;
    if (rem == 1) {
        uint32_t n = (uint32_t)data[i] << 16;
        out[o++] = table[(n >> 18) & 0x3F];
        out[o++] = table[(n >> 12) & 0x3F];
        out[o++] = '=';
        out[o++] = '=';
    } else if (rem == 2) {
        uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
        out[o++] = table[(n >> 18) & 0x3F];
        out[o++] = table[(n >> 12) & 0x3F];
        out[o++] = table[(n >> 6) & 0x3F];
        out[o++] = '=';
    }
    out[o] = '\0';
    return o;
}

// Decodes a base64 string (length must be a multiple of 4; '=' padding only
// valid in the final two positions) into out. Returns the decoded length, or
// -1 on any malformed input (bad length, character, or padding placement) or
// if it wouldn't fit in outCap - the caller maps that to a "badB64" error.
static inline long base64Decode(const char* in, size_t inLen, uint8_t* out, size_t outCap) {
    if (inLen == 0 || (inLen % 4) != 0) return -1;
    size_t o = 0;
    for (size_t g = 0; g < inLen; g += 4) {
        bool lastGroup = (g + 4 == inLen);
        int quad[4];
        int padCount = 0;
        for (int k = 0; k < 4; k++) {
            char c = in[g + k];
            if (c == '=') {
                if (!lastGroup || k < 2) return -1; // '=' only pads the last group's tail
                padCount++;
                quad[k] = 0;
            } else {
                if (padCount > 0) return -1; // no data after a pad char
                int v = base64DecodeChar(c);
                if (v < 0) return -1;
                quad[k] = v;
            }
        }
        uint32_t n = ((uint32_t)quad[0] << 18) | ((uint32_t)quad[1] << 12) |
                     ((uint32_t)quad[2] << 6) | (uint32_t)quad[3];
        int bytesOut = 3 - padCount;
        if (o + (size_t)bytesOut > outCap) return -1;
        out[o++] = (uint8_t)((n >> 16) & 0xFF);
        if (bytesOut >= 2) out[o++] = (uint8_t)((n >> 8) & 0xFF);
        if (bytesOut >= 3) out[o++] = (uint8_t)(n & 0xFF);
    }
    return (long)o;
}

#endif // TEENSY_PROTOCOL_H
