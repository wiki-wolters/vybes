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
// when the Teensy reboots, and by requestFirFilesRefresh). Copies the
// newline-separated list into dst (empty string if nothing cached yet) under
// the cache lock, so it is safe to call from any task. Returns the list length.
size_t copyCachedFirFiles(char* dst, size_t dstSize);
void requestFirFilesRefresh();

#endif // TEENSY_COMM_H
