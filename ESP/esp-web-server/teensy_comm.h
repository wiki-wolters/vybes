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
                  const char* param4 = nullptr, const char* param5 = nullptr);

// Overload for String parameters. Empty strings are treated as absent.
bool sendToTeensy(const char* command, const String& param1,
                  const String& param2 = "", const String& param3 = "", const String& param4 = "",
                  const String& param5 = "");

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
