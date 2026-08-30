#ifndef TEENSY_COMM_H
#define TEENSY_COMM_H

#include <Arduino.h>

#include "board_pins.h"
// Command names, TEENSY_MSG_MAX and the message builder live in
// teensy_protocol.h (kept Arduino-free so the Teensy's host-native test
// suite can round-trip the protocol).
#include "teensy_protocol.h"

// The Teensy link is UART2 (pins per board_pins.h). Debug output stays on
// USB - see docs/WIRING.md.
#define TeensySerial Serial2
#define TEENSY_RX_PIN PIN_TEENSY_RX
#define TEENSY_TX_PIN PIN_TEENSY_TX
#define TEENSY_BAUD 115200

// Initialise the UART link. Call once from setup() after TeensySerial is up.
void initTeensyComm();

// Queue a command for the Teensy. Never blocks: messages are drained from
// loop() by teensyCommLoop() as UART buffer space allows. Commands that set
// the same parameter (same command, and same slot for setEq/setFir)
// coalesce, so rapid UI updates don't flood the link.
// Returns false only if the queue is full.
bool sendToTeensy(const char* command, const char* param1 = nullptr,
                  const char* param2 = nullptr, const char* param3 = nullptr,
                  const char* param4 = nullptr, const char* param5 = nullptr,
                  const char* param6 = nullptr);

// Overload for String parameters. Empty strings are treated as absent.
bool sendToTeensy(const char* command, const String& param1,
                  const String& param2 = "", const String& param3 = "", const String& param4 = "",
                  const String& param5 = "", const String& param6 = "");

// Helper functions for common command types
void sendOnOffToTeensy(const char* command, bool on);
void sendIntToTeensy(const char* command, int value);
void sendFloatToTeensy(const char* command, float value);
void sendStringToTeensy(const char* command, const char* value);
void sendStringToTeensy(const char* command, const String& value);

// Drains the outgoing queue, reads incoming lines (events, ping replies,
// file lists) and handles Teensy reboot detection. Call from loop() only.
void teensyCommLoop();

// The SD file list is fetched asynchronously and cached (requested at boot,
// when the Teensy reboots, and by requestFirFilesRefresh). Each cached line
// is "name size" (V1 Teensy firmware) or just "name" (older firmware); WAV
// and TXT lines from newer firmware carry the exact tap count:
// "name size taps".
// Copies the newline-separated list into dst (empty string if nothing cached
// yet) under the cache lock, so it is safe to call from any task. Returns
// the list length.
size_t copyCachedFirFiles(char* dst, size_t dstSize);
void requestFirFilesRefresh();

// Per-output result of the last FIR load. Cleared when a load is requested,
// repopulated from the Teensy's FIRERR lines. Returns false when the output
// loaded cleanly (or has no filter assigned).
void clearFirLoadErrors();
bool getFirLoadError(int output, char* code, size_t codeSize,
                     char* file, size_t fileSize);

// Size in bytes of a cached FIR file, or -1 when the file isn't in the cache
// or was listed without a size. Safe to call from any task.
long getCachedFirFileSize(const char* name);

// Exact tap count of a cached FIR file (the third listing token, sent for
// WAV and TXT files by newer Teensy firmware), or -1 when absent. Safe to
// call from any task.
long getCachedFirFileTaps(const char* name);

// --- FIR file upload/delete (docs/AUTO_FIR_CONTRACTS.md, Slice A) ---
//
// Unlike every other Teensy command, these block the calling (httpd) task
// until the matching reply arrives or timeoutMs elapses: the HTTP contract
// needs the Teensy's actual FIRPUT/FIRDEL outcome (name/size/taps, or the
// ERR reason) in the response body, not a fire-and-forget "requested". The
// wire write still goes through the normal sendToTeensy() queue (marked as
// an ordered/non-coalescing command, like setConfigHold), so ordering with
// every other command is preserved; only the wait for the reply is new.
//
// Single-flight guard: only one upload or delete may be in flight at a
// time (contract: 409 busy). Callers must pair a successful TryBegin with
// exactly one Release.
bool firUploadTryBegin();
void firUploadRelease();

// True if "firPutBegin <name> <size> <crc32>" fits TEENSY_MSG_MAX. Nearly
// always true (FIR_FILENAME_LEN's 63 chars plus the size/crc32 overhead can
// exceed the 80-byte line for the longest names combined with a >=5-digit
// size - the wizard's own naming scheme is nowhere near this bound). Callers
// should reject with 400 rather than send a line that would be silently
// truncated.
bool firUploadNameFits(const char* name, uint32_t size);

enum FirUploadStatus { FIR_UPLOAD_OK, FIR_UPLOAD_ERR, FIR_UPLOAD_TIMEOUT };

// Sends "firPutBegin <name> <size> <crc32hex>" and blocks for FIRPUT
// BEGIN/ERR. errReason (>=16 bytes) is filled on FIR_UPLOAD_ERR.
FirUploadStatus firUploadBegin(const char* name, uint32_t size, const char* crc32hex,
                               unsigned long timeoutMs, char* errReason, size_t errReasonSize);

// Sends "firPut <seq> <base64>", first blocking (if needed) for the ESP's
// own flow-control window - at most FIR_PUT_ACK_STRIDE lines beyond the
// last ACK'd seq - to clear. Returns FIR_UPLOAD_ERR if the Teensy aborted
// the transfer (an ERR arrived, at this line or an earlier one not yet
// observed) instead of sending the line.
FirUploadStatus firUploadPutLine(int32_t seq, const char* base64Payload,
                                 unsigned long timeoutMs, char* errReason, size_t errReasonSize);

// Sends "firPutEnd" and blocks for FIRPUT OK/ERR.
FirUploadStatus firUploadEnd(unsigned long timeoutMs, uint32_t* outSize, uint32_t* outTaps,
                             char* errReason, size_t errReasonSize);

// Fire-and-forget "firPutAbort" - best-effort cleanup after an ESP-side
// failure (timeout, disconnect, oversize body) so the Teensy's temp file
// doesn't linger. Does not wait for FIRPUT STOP.
void firUploadAbort();

// Sends "firDelete <name>" and blocks for FIRDEL OK/ERR. notFound is set
// when the reason is "notfound" (maps to HTTP 404 rather than 502).
FirUploadStatus firUploadDelete(const char* name, unsigned long timeoutMs,
                                char* errReason, size_t errReasonSize, bool* notFound);

// --- SD recorder / player (see the recorder section of teensy_protocol.h) ---

// Mirror of the Teensy's last "REC STATE" line. Recording names are
// "rec-NNN.wav"; 48 covers anything the Teensy accepts on the wire.
struct RecorderState {
    bool sdPresent = false;
    bool recording = false;
    char recordFile[48] = "";
    uint32_t recordSeconds = 0;
    bool playing = false;
    char playFile[48] = "";
    uint32_t playSeconds = 0;
    uint32_t playLength = 0;
};

// Copy the current recorder state under the cache lock. Safe from any task.
void getRecorderState(RecorderState& out);

// True while a recording is running - the gate for preset switches, FIR
// edits and restores (a FIR load stalls the Teensy's loop() longer than its
// record queues can buffer). Safe from any task.
bool isRecordingActive();

// The cached recordings list ("name bytes seconds" lines, like the FIR
// cache) and its SD flag, refreshed asynchronously from "RECFILES" replies.
size_t copyCachedRecordings(char* dst, size_t dstSize);
bool recordingsSdPresent();
void requestRecordingsRefresh();

#endif // TEENSY_COMM_H
