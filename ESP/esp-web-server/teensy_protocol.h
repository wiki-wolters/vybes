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
