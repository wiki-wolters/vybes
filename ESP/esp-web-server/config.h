#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <ArduinoJson.h>

// --- Constants ---
// V1 is a clean break for the 8-output ESP32-S3 + Teensy hardware
// (docs/CHANNEL_ARCHITECTURE.md); there is no migration path from the old
// 3-channel config. The version field and migrate_config hook exist from
// day one so future schema changes migrate in place instead of silently
// reinterpreting fields.
//
// Version history:
// 1 - Initial 8-output V1 schema
// 2 - Added deviceName (multi-device support; older files default to "vybes")
// 3 - Added per-preset dynamics (mixed-input multiband compressor); absent
//     sections parse to defaults (disabled), so no doc rewrite is needed
#define CONFIG_CURRENT_VERSION 3

#define MAX_PRESETS 12
// Long enough for the contract suite's generated "contract-test-…" names
#define PRESET_NAME_MAX_LEN 48

// These must match mock-server/templates.js and the contract suite
#define NUM_OUTPUTS 8
#define MAX_CROSSOVER_POINTS 4
#define MAX_OUTPUT_PEQ 10
#define MAX_PEQ_SETS 3
#define MAX_PEQ_POINTS 15   // input EQ points per SPL set
#define FIR_TAP_POOL 12288  // taps shared across all outputs
#define MAX_DELAY_US 20000
#define OUTPUT_GAIN_MIN_DB -40.0
#define OUTPUT_GAIN_MAX_DB 10.0

#define OUTPUT_LABEL_MAX_LEN 24
#define XOVER_ID_MAX_LEN 15
#define TEMPLATE_ID_MAX_LEN 15
#define FIR_FILENAME_LEN 63

// Device name: one DNS label ("<name>.local"), also the standalone AP SSID.
// Kept well under both the 63-char DNS label and 32-char SSID limits.
#define DEVICE_NAME_MAX_LEN 24
#define DEVICE_NAME_DEFAULT "vybes"

extern const char* CONFIG_FILE;

// --- Data Structures ---

// Represents a single Parametric EQ point
struct PEQPoint {
    float freq = 1000.0f;
    float gain = 0.0f;
    float q = 1.0f;
};

// Represents a set of PEQs for a specific SPL. spl == -1 means the slot is unused.
struct PEQSet {
    int spl = -1;
    PEQPoint points[MAX_PEQ_POINTS];
    int num_points = 0; // Number of active PEQ points in this set
};

// Shared input EQ (preference curve + SPL sets) applied to the L/R buses
// ahead of the routing matrix
struct InputEq {
    bool enabled = false;
    PEQSet sets[MAX_PEQ_SETS];
};

// A named, shared crossover point. Output filters reference it by id so one
// edit moves every filter that uses it. locked points reject writes without
// confirm=true (driver protection); min/max bound the frequency.
struct CrossoverPoint {
    char id[XOVER_ID_MAX_LEN + 1] = "";
    uint16_t freq = 80;
    char type[4] = "LR4"; // LR2 | LR4 | BW2
    bool locked = false;
    uint16_t min = 20;
    uint16_t max = 20000;
};

enum class FilterMode : uint8_t { Off, Xover, Manual };

// One HP or LP section of an output. In Xover mode the frequency and type
// come from the referenced crossover point; the xover id is kept while the
// mode is Off so re-enabling restores the reference. freq/type only apply
// in Manual mode.
struct FilterSection {
    FilterMode mode = FilterMode::Off;
    char xover[XOVER_ID_MAX_LEN + 1] = "";
    double freq = 0.0; // double: echoed verbatim by the API, see Output
    char type[4] = "LR4";
};

// One of the eight output channels.
// source/gainDb/delayUs are doubles: the API echoes these values back
// verbatim and the contract suite compares some of them exactly, so they
// must survive a JSON round-trip without float32 noise (0.7f != 0.7).
struct Output {
    char label[OUTPUT_LABEL_MAX_LEN + 1] = "";
    bool enabled = false;
    double sourceLeft = 0.0;
    double sourceRight = 0.0;
    FilterSection hp;
    FilterSection lp;
    uint16_t hpFloor = 0; // Hz, 0 = none; see hp_floor_violation
    PEQPoint peq[MAX_OUTPUT_PEQ];
    int num_peq = 0;
    bool eqEnabled = true; // PEQ bypass; the points above are kept either way
    char fir[FIR_FILENAME_LEN + 1] = ""; // filename on the Teensy SD, "" = none
    double delayUs = 0.0;
    double gainDb = 0.0;
    bool invert = false;
    bool mute = false;
};

// One band of the mixed-input multiband compressor. Ranges are clamped by
// the API handler and again by the Teensy.
struct CompBand {
    float threshold = -24.0f; // dBFS
    float ratio = 2.0f;       // 1..20, 1 = no compression
    float attack = 10.0f;     // ms
    float release = 150.0f;   // ms
    float makeup = 0.0f;      // dB
    bool bypass = false;
};

#define COMP_BANDS 3
#define COMP_MODE_MAX_LEN 15

// Mixed-input multiband compressor ("dynamics" in the API/UI). mode is a
// UI label ("off" | "voice" | "night" | "punch" | "custom") - the numeric
// fields are what the device actually runs; the label just tells the UI
// which chip to highlight.
struct Dynamics {
    bool enabled = false;
    char mode[COMP_MODE_MAX_LEN + 1] = "voice";
    float strength = 70.0f;     // % of the computed reduction to apply
    float xoverLow = 250.0f;    // bass/mid split, Hz
    float xoverHigh = 4000.0f;  // mid/treble split, Hz
    float voicePriority = 6.0f; // extra bass duck while the mid band is active, dB
    CompBand bands[COMP_BANDS]; // 0 bass, 1 mid/voice, 2 treble
};

// Represents a single preset (V1 schema)
struct Preset {
    char name[PRESET_NAME_MAX_LEN] = "";
    char templateId[TEMPLATE_ID_MAX_LEN + 1] = "2.1"; // or "custom" once edited beyond it
    CrossoverPoint crossovers[MAX_CROSSOVER_POINTS];
    int num_crossovers = 0;
    InputEq inputEq;
    Output outputs[NUM_OUTPUTS];
    bool delaysEnabled = false;
    bool firEnabled = false;
    Dynamics dynamics;
};

struct SpeakerGains {
    float left = 1.0f;
    float right = 1.0f;
    float sub = 1.0f;
};

struct InputGains {
    float spdif = 1.0f;
    float bluetooth = 1.0f;
    float usb = 1.0f;
    float tone = 0.0f;
    float analog = 1.0f;
};

// Main configuration structure that holds everything
struct Config {
    uint8_t version = CONFIG_CURRENT_VERSION; // Current version of the config structure
    // Network identity: "<deviceName>.local" via mDNS and the standalone AP
    // SSID. Configurable so several Vybes devices can share one network.
    char deviceName[DEVICE_NAME_MAX_LEN + 1] = DEVICE_NAME_DEFAULT;
    int active_preset_index = 0;
    Preset presets[MAX_PRESETS];
    // Add other global settings here if needed
    int toneFrequency = 0;
    int toneVolume = 0;
    int noiseVolume = 0;

    // System states from old systemSettings. speakerGains is legacy-only:
    // the remote/button and /gains/speaker still use it until they are
    // reworked onto output gains.
    bool muted = false;
    int mutePercent = 0;            // 0-100
    SpeakerGains speakerGains;
    InputGains inputGains;
    int volume = 50; // 0-100
};

// --- Global Configuration Variable ---
extern Config current_config;

// current_config is shared between the two httpd server tasks (API handlers)
// and the loop task (debounced save, IR remote, button). Handlers must hold
// this lock while mutating it, and save_config holds it while serializing,
// so a save can't capture a half-written preset. Scope a ConfigLock around
// each mutation site.
void config_lock();
void config_unlock();

struct ConfigLock {
    ConfigLock() { config_lock(); }
    ~ConfigLock() { config_unlock(); }
    ConfigLock(const ConfigLock&) = delete;
    ConfigLock& operator=(const ConfigLock&) = delete;
};

// --- Function Prototypes ---

/**
 * @brief Initializes the configuration system.
 * Call this once in setup(). It will load from LittleFS or create a default config.
 */
void init_config();

/**
 * @brief Saves the current_config struct.
 * Call this after making any changes to the configuration.
 */
void save_config();

/**
 * @brief Loads the configuration from LittleFS.
 * @return true if successful, false if there was an error.
 */
bool load_config();

/**
 * @brief Loads a configuration from an arbitrary LittleFS path into
 * current_config. Used to validate and apply restored backups.
 */
bool load_config_from(const char* path);

/**
 * @brief Resets the configuration to its default state and saves to LittleFS.
 */
void reset_config_to_defaults();

// True for a DNS-label-safe device name: 1-24 chars of lowercase a-z, 0-9
// and dashes, not starting or ending with a dash.
bool is_valid_device_name(const char* name);

// --- V1 preset model helpers (shared by the API handlers and Teensy sync) ---

// Index of the crossover point with the given id, or -1
int find_crossover_by_id(const Preset& preset, const char* id);

// The concrete frequency of a filter section: the manual value, the
// referenced crossover's frequency, or 0 when off/unresolvable.
double resolve_filter_freq(const Preset& preset, const FilterSection& section);

// The filter's slope type as a string: the crossover point's type in Xover
// mode, the section's own type in Manual mode.
const char* resolve_filter_type(const Preset& preset, const FilterSection& section);

// Driver-protection backstop: returns the index of the first enabled output
// whose effective high-pass sits below its hpFloor, or -1 when the preset is
// safe. Must hold no matter which endpoint made the edit.
int hp_floor_violation(const Preset& preset);

// Structural edits flip the preset's template to "custom" so the UI stops
// rendering the template's simple view. Returns true when this call flipped
// it (callers report "template":"custom" in that payload only).
bool flip_template_to_custom(Preset& preset);

// JSON serialization of model pieces (shared by GET /preset and broadcasts)
void filter_to_json(const FilterSection& section, JsonObject obj);
void crossover_to_json(const CrossoverPoint& point, JsonObject obj);
void output_to_json(const Output& output, JsonObject obj);
void input_eq_to_json(const InputEq& eq, JsonObject obj);
void dynamics_to_json(const Dynamics& dyn, JsonObject obj);

void updateTeensyWithActivePresetParameters();

// Queue a single shared-input-EQ point for the Teensy (band index + freq/q/gain)
void sendInputEqPointToTeensy(int index, const PEQPoint& point);

// Queue a single output-PEQ point for the Teensy
void sendOutputEqPointToTeensy(int channel, int band, const PEQPoint& point);

// Queue the resolved HP+LP sections of one output for the Teensy
void sendOutputFiltersToTeensy(int channel, const Preset& preset);

// Queue the full dynamics (multiband compressor) state for the Teensy
void sendDynamicsToTeensy(const Dynamics& dyn);

void loadFirFilters();

#endif // CONFIG_H
