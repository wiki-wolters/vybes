#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// --- Constants ---
#define EEPROM_SIZE 4096
#define CONFIG_MAGIC_VALUE 0xDEADBEEF
#define CONFIG_CURRENT_VERSION 2

// Version history:
// 1 - Initial version
// 2 - Added version field and PEQ support

#define MAX_PRESETS 8
#define MAX_PEQ_SETS 4
#define MAX_PEQ_POINTS 8
#define PRESET_NAME_MAX_LEN 16

// --- Data Structures ---

// Represents a single Parametric EQ point
struct PEQPoint {
    float freq = 1000.0f;
    float gain = 0.0f;
    float q = 1.0f;
};

// Represents a set of PEQs for a specific SPL
struct PEQSet {
    int spl = 0;
    PEQPoint points[MAX_PEQ_POINTS];
    int num_points = 0; // Number of active PEQ points in this set
};

// Represents delay settings for speakers
struct Delay {
    float left = 0.0f;
    float right = 0.0f;
    float sub = 0.0f;
};

struct FIRFilter {
    String left = "";
    String right = "";
    String sub = "";
};

struct SpeakerGains {
    float left = 1.0f;
    float right = 1.0f;
    float sub = 1.0f;
};

struct InputGains {
    float spdif = 1.0f;
    float bluetooth = 1.0f;
}

// Represents a single preset
struct Preset {
    char name[PRESET_NAME_MAX_LEN] = "Default";
    Delay delay;
    bool delayEnabled = false;
    int8_t crossoverFreq = 80;
    bool crossoverEnabled = false;
    PEQSet room_correction[MAX_PEQ_SETS];
    PEQSet preference_curve[MAX_PEQ_SETS];
    bool EQEnabled = false;
    bool FIRFiltersEnabled = false;
    FIRFilter FIRFilters;
};

// Main configuration structure that holds everything
struct Config {
    uint32_t magic_value = 0; // To check if EEPROM is initialized
    uint8_t version = CONFIG_CURRENT_VERSION; // Current version of the config structure
    int active_preset_index = 0;
    Preset presets[MAX_PRESETS];
    // Add other global settings here if needed
    int toneFrequency = 0;
    int toneVolume = 0;
    int noiseVolume = 0;

    // System states from old systemSettings
    char muteState[4] = "off";      // "on" or "off"
    int mutePercent = 0;            // 0-100
    SpeakerGains speakerGains;
    InputGains inputGains;
};

// --- Global Configuration Variable ---
extern Config current_config;

// --- Function Prototypes ---

/**
 * @brief Initializes the configuration system.
 * Call this once in setup(). It will load from EEPROM or create a default config.
 */
void init_config();

/**
 * @brief Saves the current_config struct to EEPROM.
 * Call this after making any changes to the configuration.
 */
void save_config();

/**
 * @brief Loads the configuration from EEPROM into the current_config struct.
 * This is called by init_config() automatically.
 */
void load_config();

/**
 * @brief Resets the configuration to its default state and saves to EEPROM.
 */
void reset_config_to_defaults();


#endif // CONFIG_H
