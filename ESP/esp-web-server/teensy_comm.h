#ifndef TEENSY_COMM_H
#define TEENSY_COMM_H

#include <Arduino.h>

// Speaker and Gain Commands
#define CMD_SET_SPEAKER_GAINS "setSpeakerGains"
#define CMD_SET_INPUT_GAINS "setInputGains"

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
#define CMD_GET_FILES "getFiles"

// Delay Commands
#define CMD_SET_DELAYS "setDelays"
#define CMD_SET_DELAY_ENABLED "setDelayEnabled"

// System Commands
#define CMD_SET_SUBWOOFER "setSubwoofer"
#define CMD_SET_BYPASS "setBypass"
#define CMD_SET_MUTE "setMute"
#define CMD_SET_MUTE_PERCENT "setMutePercent"

// Global buffer for storing the last response from Teensy
extern char teensyResponse[1024];

// Send a command to the Teensy
// command: The command name (use the CMD_* constants from this file)
// param1: First parameter (optional)
// param2: Second parameter (optional)
// param3: Third parameter (optional)
// Returns: true if successful, false otherwise
// Response is stored in the teensyResponse global variable
bool sendToTeensy(const char* command, const String& param1 = "", 
                 const String& param2 = "", const String& param3 = "");

// Helper functions for common command types
void sendOnOffToTeensy(const char* command, bool on);
void sendIntToTeensy(const char* command, int value);
void sendFloatToTeensy(const char* command, float value);
void sendStringToTeensy(const char* command, const String& value);

char* requestFromTeensy(const char* command);

#endif // TEENSY_COMM_H
