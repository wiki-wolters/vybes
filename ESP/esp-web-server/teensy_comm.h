#ifndef TEENSY_COMM_H
#define TEENSY_COMM_H

#include <Arduino.h>

// The Teensy link is UART2: GPIO16 (RX2, from Teensy TX1/pin 1) and GPIO17
// (TX2, to Teensy RX1/pin 0). Debug output stays on USB - see docs/WIRING.md.
#define TeensySerial Serial2
#define TEENSY_RX_PIN 16
#define TEENSY_TX_PIN 17
#define TEENSY_BAUD 115200

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

// Initialise the UART link. Call once from setup() after TeensySerial is up.
void initTeensyComm();

// Queue a command for the Teensy. Never blocks: messages are drained from
// loop() by teensyCommLoop() as UART buffer space allows. Commands that set
// the same parameter (same command, and same slot for setEq/setFir)
// coalesce, so rapid UI updates don't flood the link.
// Returns false only if the queue is full.
bool sendToTeensy(const char* command, const char* param1 = nullptr,
                  const char* param2 = nullptr, const char* param3 = nullptr,
                  const char* param4 = nullptr);

// Overload for String parameters. Empty strings are treated as absent.
bool sendToTeensy(const char* command, const String& param1,
                  const String& param2 = "", const String& param3 = "", const String& param4 = "");

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
// when the Teensy reboots, and by requestFirFilesRefresh). Returns a
// newline-separated list; empty string if nothing cached yet.
const char* getCachedFirFiles();
void requestFirFilesRefresh();

#endif // TEENSY_COMM_H
