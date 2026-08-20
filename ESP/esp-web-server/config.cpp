#include "globals.h"
#include "config.h"
#include "templates.h"
#include "teensy_comm.h"
#include "compare_mode.h"
#include "api_fir.h"
#include "screen.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Define the global configuration instance
Config current_config;

const char* CONFIG_FILE = "/config.msgpack";

// Guards current_config against the httpd tasks mutating it while the loop
// task serializes it (see config.h). Created in init_config, which runs
// before the web servers start; until then everything is single-threaded.
static SemaphoreHandle_t configMutex = nullptr;

void config_lock() {
    if (configMutex != nullptr) {
        xSemaphoreTake(configMutex, portMAX_DELAY);
    }
}

void config_unlock() {
    if (configMutex != nullptr) {
        xSemaphoreGive(configMutex);
    }
}

bool is_valid_device_name(const char* name) {
    size_t len = name != nullptr ? strlen(name) : 0;
    if (len == 0 || len > DEVICE_NAME_MAX_LEN) {
        return false;
    }
    if (name[0] == '-' || name[len - 1] == '-') {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
            return false;
        }
    }
    return true;
}

// --- V1 preset model helpers ---

int find_crossover_by_id(const Preset& preset, const char* id) {
    for (int i = 0; i < preset.num_crossovers; i++) {
        if (strcmp(preset.crossovers[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

double resolve_filter_freq(const Preset& preset, const FilterSection& section) {
    if (section.mode == FilterMode::Manual) {
        return section.freq;
    }
    if (section.mode == FilterMode::Xover) {
        int index = find_crossover_by_id(preset, section.xover);
        return index >= 0 ? preset.crossovers[index].freq : 0.0;
    }
    return 0.0;
}

const char* resolve_filter_type(const Preset& preset, const FilterSection& section) {
    if (section.mode == FilterMode::Xover) {
        int index = find_crossover_by_id(preset, section.xover);
        if (index >= 0) {
            return preset.crossovers[index].type;
        }
    }
    return section.type;
}

int hp_floor_violation(const Preset& preset) {
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        const Output& output = preset.outputs[i];
        if (!output.enabled || output.hpFloor == 0) continue;
        if (resolve_filter_freq(preset, output.hp) < output.hpFloor) {
            return i;
        }
    }
    return -1;
}

bool flip_template_to_custom(Preset& preset) {
    if (strcmp(preset.templateId, "custom") == 0) {
        return false;
    }
    strlcpy(preset.templateId, "custom", sizeof(preset.templateId));
    return true;
}

// --- JSON serialization of model pieces ---
// These produce the API shapes (GET /preset, broadcasts) and double as the
// storage format inside /config.msgpack.

static const char* filterModeName(FilterMode mode) {
    switch (mode) {
        case FilterMode::Xover: return "xover";
        case FilterMode::Manual: return "manual";
        default: return "off";
    }
}

void filter_to_json(const FilterSection& section, JsonObject obj) {
    obj["mode"] = filterModeName(section.mode);
    if (section.mode == FilterMode::Manual) {
        obj["freq"] = section.freq;
        obj["type"] = section.type;
    } else if (section.xover[0] != '\0') {
        // Xover mode, or Off with a kept reference so re-enabling restores it
        obj["xover"] = section.xover;
    }
}

void crossover_to_json(const CrossoverPoint& point, JsonObject obj) {
    obj["id"] = point.id;
    obj["freq"] = point.freq;
    obj["type"] = point.type;
    obj["locked"] = point.locked;
    obj["min"] = point.min;
    obj["max"] = point.max;
}

void output_to_json(const Output& output, JsonObject obj) {
    obj["label"] = output.label;
    obj["enabled"] = output.enabled;
    JsonObject source = obj.createNestedObject("source");
    source["left"] = output.sourceLeft;
    source["right"] = output.sourceRight;
    filter_to_json(output.hp, obj.createNestedObject("hp"));
    filter_to_json(output.lp, obj.createNestedObject("lp"));
    obj["hpFloor"] = output.hpFloor;
    JsonArray peq = obj.createNestedArray("peq");
    for (int i = 0; i < output.num_peq; i++) {
        JsonObject point = peq.createNestedObject();
        point["freq"] = output.peq[i].freq;
        point["gain"] = output.peq[i].gain;
        point["q"] = output.peq[i].q;
    }
    obj["eqEnabled"] = output.eqEnabled;
    obj["fir"] = output.fir;
    obj["delayUs"] = output.delayUs;
    obj["gainDb"] = output.gainDb;
    obj["invert"] = output.invert;
    obj["mute"] = output.mute;
}

void input_eq_to_json(const InputEq& eq, JsonObject obj) {
    obj["enabled"] = eq.enabled;
    JsonArray sets = obj.createNestedArray("sets");
    for (int i = 0; i < MAX_PEQ_SETS; i++) {
        if (eq.sets[i].spl == -1) continue;
        JsonObject set = sets.createNestedObject();
        set["spl"] = eq.sets[i].spl;
        JsonArray points = set.createNestedArray("points");
        for (int j = 0; j < eq.sets[i].num_points; j++) {
            JsonObject point = points.createNestedObject();
            point["freq"] = eq.sets[i].points[j].freq;
            point["gain"] = eq.sets[i].points[j].gain;
            point["q"] = eq.sets[i].points[j].q;
        }
    }
}

void dynamics_to_json(const Dynamics& dyn, JsonObject obj) {
    obj["enabled"] = dyn.enabled;
    obj["mode"] = dyn.mode;
    obj["strength"] = dyn.strength;
    obj["xoverLow"] = dyn.xoverLow;
    obj["xoverHigh"] = dyn.xoverHigh;
    obj["voicePriority"] = dyn.voicePriority;
    JsonArray bands = obj.createNestedArray("bands");
    for (int i = 0; i < COMP_BANDS; i++) {
        JsonObject band = bands.createNestedObject();
        band["threshold"] = dyn.bands[i].threshold;
        band["ratio"] = dyn.bands[i].ratio;
        band["attack"] = dyn.bands[i].attack;
        band["release"] = dyn.bands[i].release;
        band["makeup"] = dyn.bands[i].makeup;
        band["bypass"] = dyn.bands[i].bypass;
    }
}

// --- JSON parsing (storage load) ---

static void filter_from_json(JsonObject obj, FilterSection& section) {
    section = FilterSection();
    if (obj.isNull()) return;
    const char* mode = obj["mode"] | "off";
    if (strcmp(mode, "xover") == 0) {
        section.mode = FilterMode::Xover;
    } else if (strcmp(mode, "manual") == 0) {
        section.mode = FilterMode::Manual;
    } else {
        section.mode = FilterMode::Off;
    }
    strlcpy(section.xover, obj["xover"] | "", sizeof(section.xover));
    section.freq = obj["freq"] | 0.0;
    strlcpy(section.type, obj["type"] | "LR4", sizeof(section.type));
}

static void peq_points_from_json(JsonArray array, PEQPoint* points, int maxPoints, int& count) {
    count = 0;
    if (array.isNull()) return;
    for (JsonObject point : array) {
        if (count >= maxPoints) break;
        points[count].freq = point["freq"] | 1000.0f;
        points[count].gain = point["gain"] | 0.0f;
        points[count].q = point["q"] | 1.0f;
        count++;
    }
}

static void output_from_json(JsonObject obj, Output& output, int index) {
    output = Output();
    snprintf(output.label, sizeof(output.label), "Out %d", index + 1);
    if (obj.isNull()) return;
    strlcpy(output.label, obj["label"] | output.label, sizeof(output.label));
    output.enabled = obj["enabled"] | false;
    JsonObject source = obj["source"];
    output.sourceLeft = source["left"] | 0.0;
    output.sourceRight = source["right"] | 0.0;
    filter_from_json(obj["hp"], output.hp);
    filter_from_json(obj["lp"], output.lp);
    output.hpFloor = obj["hpFloor"] | 0;
    peq_points_from_json(obj["peq"], output.peq, MAX_OUTPUT_PEQ, output.num_peq);
    output.eqEnabled = obj["eqEnabled"] | true; // absent in older configs
    strlcpy(output.fir, obj["fir"] | "", sizeof(output.fir));
    output.delayUs = obj["delayUs"] | 0.0;
    output.gainDb = obj["gainDb"] | 0.0;
    output.invert = obj["invert"] | false;
    output.mute = obj["mute"] | false;
}

static void preset_from_json(JsonObject obj, Preset& preset) {
    strlcpy(preset.templateId, obj["template"] | DEFAULT_TEMPLATE_ID, sizeof(preset.templateId));

    preset.num_crossovers = 0;
    for (JsonObject point : obj["crossovers"].as<JsonArray>()) {
        if (preset.num_crossovers >= MAX_CROSSOVER_POINTS) break;
        CrossoverPoint& xo = preset.crossovers[preset.num_crossovers++];
        strlcpy(xo.id, point["id"] | "", sizeof(xo.id));
        xo.freq = point["freq"] | 80;
        strlcpy(xo.type, point["type"] | "LR4", sizeof(xo.type));
        xo.locked = point["locked"] | false;
        xo.min = point["min"] | 20;
        xo.max = point["max"] | 20000;
    }

    JsonObject inputEq = obj["inputEq"];
    preset.inputEq.enabled = inputEq["enabled"] | false;
    for (int i = 0; i < MAX_PEQ_SETS; i++) {
        preset.inputEq.sets[i] = PEQSet();
    }
    int setCount = 0;
    for (JsonObject set : inputEq["sets"].as<JsonArray>()) {
        if (setCount >= MAX_PEQ_SETS) break;
        PEQSet& target = preset.inputEq.sets[setCount++];
        target.spl = set["spl"] | 0;
        peq_points_from_json(set["points"], target.points, MAX_PEQ_POINTS, target.num_points);
    }

    JsonArray outputs = obj["outputs"];
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        output_from_json(outputs[i], preset.outputs[i], i);
    }

    preset.delaysEnabled = obj["delaysEnabled"] | false;
    preset.firEnabled = obj["firEnabled"] | false;

    // Absent before v4 (the migration seeds it); clamp so a hand-edited or
    // corrupt file can't hand the Teensy an out-of-range master gain
    int volume = obj["volume"] | PRESET_VOLUME_DEFAULT;
    preset.volume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);

    // Absent in configs saved before v3 - defaults leave it disabled
    preset.dynamics = Dynamics();
    JsonObject dyn = obj["dynamics"];
    if (!dyn.isNull()) {
        preset.dynamics.enabled = dyn["enabled"] | false;
        strlcpy(preset.dynamics.mode, dyn["mode"] | "voice", sizeof(preset.dynamics.mode));
        preset.dynamics.strength = dyn["strength"] | 70.0f;
        preset.dynamics.xoverLow = dyn["xoverLow"] | 250.0f;
        preset.dynamics.xoverHigh = dyn["xoverHigh"] | 4000.0f;
        preset.dynamics.voicePriority = dyn["voicePriority"] | 6.0f;
        int bandCount = 0;
        for (JsonObject band : dyn["bands"].as<JsonArray>()) {
            if (bandCount >= COMP_BANDS) break;
            CompBand& target = preset.dynamics.bands[bandCount++];
            target.threshold = band["threshold"] | target.threshold;
            target.ratio = band["ratio"] | target.ratio;
            target.attack = band["attack"] | target.attack;
            target.release = band["release"] | target.release;
            target.makeup = band["makeup"] | target.makeup;
            target.bypass = band["bypass"] | false;
        }
    }
}

static void preset_to_json(const Preset& preset, JsonObject obj) {
    obj["name"] = preset.name;
    obj["template"] = preset.templateId;
    JsonArray crossovers = obj.createNestedArray("crossovers");
    for (int i = 0; i < preset.num_crossovers; i++) {
        crossover_to_json(preset.crossovers[i], crossovers.createNestedObject());
    }
    input_eq_to_json(preset.inputEq, obj.createNestedObject("inputEq"));
    JsonArray outputs = obj.createNestedArray("outputs");
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        output_to_json(preset.outputs[i], outputs.createNestedObject());
    }
    obj["delaysEnabled"] = preset.delaysEnabled;
    obj["firEnabled"] = preset.firEnabled;
    obj["volume"] = preset.volume;
    dynamics_to_json(preset.dynamics, obj.createNestedObject("dynamics"));
}

// --- Load / save ---

// Schema migration hook: upgrade steps mutate doc in place before the
// normal parse runs. Returns false for versions that can't be migrated.
static bool migrate_config(JsonDocument& doc, uint8_t fromVersion) {
    if (fromVersion < 1 || fromVersion > CONFIG_CURRENT_VERSION) {
        return false;
    }
    // v1 -> v2: deviceName was added; absent keys parse to the default.
    // v2 -> v3: per-preset dynamics was added; an absent section parses to
    // defaults (disabled). No doc rewrite is needed for either.
    if (fromVersion < 4) {
        // v3 -> v4: master volume moved from global state into the presets.
        // Seed every preset with the level the device was last playing so
        // the upgrade doesn't change how anything sounds.
        int legacyVolume = doc["volume"] | PRESET_VOLUME_DEFAULT;
        for (JsonObject preset : doc["presets"].as<JsonArray>()) {
            preset["volume"] = legacyVolume;
        }
    }
    return true;
}

bool load_config() {
    return load_config_from(CONFIG_FILE);
}

bool load_config_from(const char* path) {
    if (!LittleFS.exists(path)) {
        DebugSerial.println("Config file not found, using defaults");
        return false;
    }

    File file = LittleFS.open(path, "r");
    if (!file) {
        DebugSerial.println("Failed to open config file for reading");
        return false;
    }

    // Create a buffer to hold the MessagePack data
    size_t fileSize = file.size();
    if (fileSize == 0) {
        DebugSerial.println("Config file is empty");
        file.close();
        return false;
    }

    std::unique_ptr<char[]> buffer(new char[fileSize]);
    file.readBytes(buffer.get(), fileSize);
    file.close();

    // Deserialize MessagePack
    JsonDocument doc;
    DeserializationError error = deserializeMsgPack(doc, buffer.get(), fileSize);

    if (error) {
        DebugSerial.print("Failed to deserialize config: ");
        DebugSerial.println(error.c_str());
        return false;
    }

    uint8_t file_version = doc["version"] | 0;
    if (file_version > CONFIG_CURRENT_VERSION) {
        DebugSerial.println("Config file version is newer than supported, using defaults");
        return false;
    }
    if (!migrate_config(doc, file_version)) {
        DebugSerial.println("Config file version is not migratable, using defaults");
        return false;
    }

    // Load global settings
    current_config.version = CONFIG_CURRENT_VERSION;
    // A corrupt/hand-edited name must stay a valid DNS label
    const char* deviceName = doc["deviceName"] | DEVICE_NAME_DEFAULT;
    strlcpy(current_config.deviceName,
            is_valid_device_name(deviceName) ? deviceName : DEVICE_NAME_DEFAULT,
            sizeof(current_config.deviceName));
    // Clamp: a corrupt/hand-edited file must not index outside presets[]
    int active_preset_index = doc["active_preset_index"] | 0;
    if (active_preset_index < 0 || active_preset_index >= MAX_PRESETS) {
        active_preset_index = 0;
    }
    current_config.active_preset_index = active_preset_index;
    current_config.toneFrequency = doc["toneFrequency"] | 0;
    current_config.toneVolume = doc["toneVolume"] | 0;
    current_config.noiseVolume = doc["noiseVolume"] | 0;
    current_config.muted = doc["muted"] | false;
    current_config.mutePercent = doc["mutePercent"] | 0;

    JsonObject speakerGains = doc["speakerGains"];
    current_config.speakerGains.left = speakerGains["left"] | 1.0f;
    current_config.speakerGains.right = speakerGains["right"] | 1.0f;
    current_config.speakerGains.sub = speakerGains["sub"] | 1.0f;

    JsonObject inputGains = doc["inputGains"];
    current_config.inputGains.spdif = inputGains["spdif"] | 1.0f;
    current_config.inputGains.bluetooth = inputGains["bluetooth"] | 1.0f;
    current_config.inputGains.usb = inputGains["usb"] | 1.0f;
    current_config.inputGains.tone = inputGains["tone"] | 0.0f;
    current_config.inputGains.analog = inputGains["analog"] | 1.0f;

    // Load presets. Every slot is reset first so fields absent from the file
    // (and stale state from a previous config, e.g. during a restore) don't
    // leak through. Only slots with a name carry data.
    JsonArray presets = doc["presets"];
    for (int i = 0; i < MAX_PRESETS; i++) {
        Preset& preset = current_config.presets[i];
        JsonObject obj = presets[i];
        const char* name = obj["name"] | "";
        strlcpy(preset.name, name, sizeof(preset.name));
        if (preset.name[0] != '\0') {
            preset_from_json(obj, preset);
        } else {
            // In-place reset (no whole-Preset stack temporary)
            build_preset_from_template(preset, DEFAULT_TEMPLATE_ID);
            preset.name[0] = '\0';
        }
    }

    DebugSerial.println("Config loaded successfully");
    return true;
}

void save_config() {
    DebugSerial.println("Saving configuration to LittleFS...");

    // Snapshot current_config into a JSON document under the config lock so
    // an API handler can't mutate it mid-serialization. The lock is released
    // before the slow flash writes below.
    JsonDocument doc;
    config_lock();

    doc["version"] = current_config.version;
    doc["deviceName"] = current_config.deviceName;
    doc["active_preset_index"] = current_config.active_preset_index;
    doc["toneFrequency"] = current_config.toneFrequency;
    doc["toneVolume"] = current_config.toneVolume;
    doc["noiseVolume"] = current_config.noiseVolume;
    doc["muted"] = current_config.muted;
    doc["mutePercent"] = current_config.mutePercent;

    JsonObject speakerGains = doc.createNestedObject("speakerGains");
    speakerGains["left"] = current_config.speakerGains.left;
    speakerGains["right"] = current_config.speakerGains.right;
    speakerGains["sub"] = current_config.speakerGains.sub;

    JsonObject inputGains = doc.createNestedObject("inputGains");
    inputGains["spdif"] = current_config.inputGains.spdif;
    inputGains["bluetooth"] = current_config.inputGains.bluetooth;
    inputGains["usb"] = current_config.inputGains.usb;
    inputGains["tone"] = current_config.inputGains.tone;
    inputGains["analog"] = current_config.inputGains.analog;

    // Save presets, preserving slot positions (active_preset_index and the
    // button/remote cycling are slot-based). Empty slots save name-only.
    JsonArray presets = doc.createNestedArray("presets");
    for (int i = 0; i < MAX_PRESETS; i++) {
        JsonObject preset = presets.createNestedObject();
        if (strlen(current_config.presets[i].name) > 0) {
            preset_to_json(current_config.presets[i], preset);
        } else {
            preset["name"] = "";
        }
    }

    config_unlock();

    // Serialize to MessagePack. Write to a temp file first, then rename over
    // the live config so a power loss mid-write can't corrupt it.
    const char* tmpFile = "/config.tmp";
    File file = LittleFS.open(tmpFile, "w");
    if (!file) {
        DebugSerial.println("Failed to open temp config file for writing");
        return;
    }

    size_t bytesWritten = serializeMsgPack(doc, file);
    file.close();

    if (bytesWritten == 0) {
        DebugSerial.println("Failed to write config file");
        LittleFS.remove(tmpFile);
        return;
    }

    if (!LittleFS.rename(tmpFile, CONFIG_FILE)) {
        // Some FS implementations refuse to rename over an existing file
        LittleFS.remove(CONFIG_FILE);
        if (!LittleFS.rename(tmpFile, CONFIG_FILE)) {
            DebugSerial.println("Failed to move temp config into place");
            return;
        }
    }

    DebugSerial.print("Config saved successfully, ");
    DebugSerial.print(bytesWritten);
    DebugSerial.println(" bytes written");
}

void reset_config_to_defaults() {
    DebugSerial.println("Resetting configuration to defaults...");

    current_config.version = CONFIG_CURRENT_VERSION;
    strlcpy(current_config.deviceName, DEVICE_NAME_DEFAULT, sizeof(current_config.deviceName));
    current_config.active_preset_index = 0;
    current_config.toneFrequency = 0;
    current_config.toneVolume = 0;
    current_config.noiseVolume = 0;
    current_config.muted = false;
    current_config.mutePercent = 0;

    current_config.speakerGains = SpeakerGains();
    current_config.inputGains = InputGains();

    // First preset is 'Default' on the default template; the rest are unused
    for (int i = 0; i < MAX_PRESETS; i++) {
        Preset& preset = current_config.presets[i];
        build_preset_from_template(preset, DEFAULT_TEMPLATE_ID);
        preset.name[0] = '\0';
    }
    strlcpy(current_config.presets[0].name, "Default", sizeof(current_config.presets[0].name));
}

void init_config() {
    configMutex = xSemaphoreCreateMutex();

    // Initialize LittleFS
    if (!LittleFS.begin()) {
        DebugSerial.println("Failed to initialize LittleFS");
        reset_config_to_defaults();
        return;
    }

    // Try to load existing config
    if (!load_config()) {
        // If loading fails, use defaults and save them
        reset_config_to_defaults();
        save_config();
    }

    updateTeensyWithActivePresetParameters();
}

// --- Teensy sync ---
// The Teensy is dumb and per-channel: it never sees crossover ids or
// templates. Everything below resolves references to concrete values first.

void sendInputEqPointToTeensy(int index, const PEQPoint& point) {
    char idStr[8];
    snprintf(idStr, sizeof(idStr), "%d", index);
    char pointData[40];
    snprintf(pointData, sizeof(pointData), "%.1f %.2f %.2f", point.freq, point.q, point.gain);
    sendToTeensy(CMD_SET_INPUT_EQ, idStr, pointData);
}

void sendOutputEqPointToTeensy(int channel, int band, const PEQPoint& point) {
    char chStr[8], bandStr[8];
    snprintf(chStr, sizeof(chStr), "%d", channel);
    snprintf(bandStr, sizeof(bandStr), "%d", band);
    char pointData[40];
    snprintf(pointData, sizeof(pointData), "%.1f %.2f %.2f", point.freq, point.q, point.gain);
    sendToTeensy(CMD_SET_OUTPUT_EQ, chStr, bandStr, pointData);
}

void sendOutputFiltersToTeensy(int channel, const Preset& preset) {
    const Output& output = preset.outputs[channel];
    char chStr[8], freqStr[16];
    snprintf(chStr, sizeof(chStr), "%d", channel);
    // freq 0 = section off; the type still travels so the message shape is fixed
    snprintf(freqStr, sizeof(freqStr), "%.1f", resolve_filter_freq(preset, output.hp));
    sendToTeensy(CMD_SET_OUTPUT_HP, chStr, freqStr, resolve_filter_type(preset, output.hp));
    snprintf(freqStr, sizeof(freqStr), "%.1f", resolve_filter_freq(preset, output.lp));
    sendToTeensy(CMD_SET_OUTPUT_LP, chStr, freqStr, resolve_filter_type(preset, output.lp));
}

void sendDynamicsToTeensy(const Dynamics& dyn) {
    char a[16], b[16];
    snprintf(a, sizeof(a), "%.1f", dyn.xoverLow);
    snprintf(b, sizeof(b), "%.1f", dyn.xoverHigh);
    sendToTeensy(CMD_SET_COMP_XOVER, a, b);
    for (int i = 0; i < COMP_BANDS; i++) {
        char idx[4];
        snprintf(idx, sizeof(idx), "%d", i);
        // Five values space-packed into one builder param; the Teensy's
        // router re-splits them (same trick as setInputEq's point data)
        char packed[56];
        snprintf(packed, sizeof(packed), "%.1f %.2f %.1f %.1f %.1f",
                 dyn.bands[i].threshold, dyn.bands[i].ratio, dyn.bands[i].attack,
                 dyn.bands[i].release, dyn.bands[i].makeup);
        sendToTeensy(CMD_SET_COMP_BAND, idx, packed);
        sendToTeensy(CMD_SET_COMP_BAND_BYPASS, idx, dyn.bands[i].bypass ? "1" : "0");
    }
    sendFloatToTeensy(CMD_SET_COMP_STRENGTH, dyn.strength);
    sendFloatToTeensy(CMD_SET_COMP_VOICE_PRIORITY, dyn.voicePriority);
    // Enable last, so the compressor turns on with its parameters in place
    sendOnOffToTeensy(CMD_SET_COMP_ENABLED, dyn.enabled);
}

void updateTeensyWithActivePresetParameters() {
    Preset* activePreset = &current_config.presets[current_config.active_preset_index];

    // Silence the outputs for the duration of the sync. Everything below
    // lands one command at a time and the Teensy would otherwise play each
    // half-applied state on the way through - see CMD_SET_CONFIG_HOLD.
    sendOnOffToTeensy(CMD_SET_CONFIG_HOLD, true);

    //Update displayed preset name
    DebugSerial.print("Updating screen: ");DebugSerial.print(current_config.active_preset_index);DebugSerial.print(" ");DebugSerial.println(activePreset->name);
    writeToScreen(activePreset->name);

    char a[16], b[16], c[16], d[16], e[16];

    // Per-output channel state, crossover references resolved to frequencies
    for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
        const Output& output = activePreset->outputs[ch];
        snprintf(a, sizeof(a), "%d", ch);

        snprintf(b, sizeof(b), "%.2f", output.gainDb);
        sendToTeensy(CMD_SET_OUTPUT_GAIN, a, b);

        // A disabled output is just a muted one as far as the DSP goes
        sendToTeensy(CMD_SET_OUTPUT_MUTE, a, (output.mute || !output.enabled) ? "1" : "0");
        sendToTeensy(CMD_SET_OUTPUT_INVERT, a, output.invert ? "1" : "0");

        snprintf(b, sizeof(b), "%d", (int)output.delayUs);
        sendToTeensy(CMD_SET_OUTPUT_DELAY, a, b);

        sendOutputFiltersToTeensy(ch, *activePreset);

        for (int band = 0; band < output.num_peq; band++) {
            sendOutputEqPointToTeensy(ch, band, output.peq[band]);
        }
        // Disable all bands beyond the active points
        snprintf(b, sizeof(b), "%d", output.num_peq);
        sendToTeensy(CMD_RESET_OUTPUT_EQ, a, b);

        sendToTeensy(CMD_SET_OUTPUT_EQ_ENABLED, a, output.eqEnabled ? "1" : "0");

        // Bare "setFir <ch>" clears the filter
        sendToTeensy(CMD_SET_FIR, a, output.fir[0] != '\0' ? output.fir : nullptr);

        // Routing last, the same discipline the compressor uses below: the
        // source mix is what makes a channel audible at all, so it goes on
        // only once this channel's gain, crossover, EQ and FIR are set. On
        // its own that closes the window even if the hold is unavailable.
        snprintf(b, sizeof(b), "%.4f", output.sourceLeft);
        snprintf(c, sizeof(c), "%.4f", output.sourceRight);
        sendToTeensy(CMD_SET_OUTPUT_SOURCE, a, b, c);
    }

    // Preset-level master toggles
    sendOnOffToTeensy(CMD_SET_DELAYS_ENABLED, activePreset->delaysEnabled);
    sendOnOffToTeensy(CMD_SET_FIR_ENABLED, activePreset->firEnabled);

    // Mixed-input dynamics
    sendDynamicsToTeensy(activePreset->dynamics);

    // Shared input EQ. Points are always sent (even when EQ is disabled) so
    // the Teensy has the right curve the moment EQ is enabled.
    sendOnOffToTeensy(CMD_SET_INPUT_EQ_ENABLED, activePreset->inputEq.enabled);
    int num_points = 0;
    for (int i = 0; i < MAX_PEQ_SETS; i++) {
        if (activePreset->inputEq.sets[i].spl == 0) {
            const PEQSet& set = activePreset->inputEq.sets[i];
            for (int j = 0; j < set.num_points; j++) {
                sendInputEqPointToTeensy(j, set.points[j]);
            }
            num_points = set.num_points;
            break;
        }
    }
    // Disable all bands beyond the active points
    snprintf(a, sizeof(a), "%d", num_points);
    sendToTeensy(CMD_RESET_INPUT_EQ, a);

    // Send volume (per-preset) and mute state
    sendFloatToTeensy(CMD_SET_VOLUME, activePreset->volume / 100.0f);
    sendOnOffToTeensy(CMD_SET_MUTE, current_config.muted);
    sendFloatToTeensy(CMD_SET_MUTE_PERCENT, current_config.mutePercent);

    // Legacy global speaker gains (remote/button path, until reworked)
    snprintf(a, sizeof(a), "%.2f", current_config.speakerGains.left);
    snprintf(b, sizeof(b), "%.2f", current_config.speakerGains.right);
    snprintf(c, sizeof(c), "%.2f", current_config.speakerGains.sub);
    sendToTeensy(CMD_SET_SPEAKER_GAINS, a, b, c);

    // Send input gains (order matches Teensy handler: bluetooth, spdif/optical, usb, tone, analog)
    snprintf(a, sizeof(a), "%.2f", current_config.inputGains.bluetooth);
    snprintf(b, sizeof(b), "%.2f", current_config.inputGains.spdif);
    snprintf(c, sizeof(c), "%.2f", current_config.inputGains.usb);
    snprintf(d, sizeof(d), "%.2f", current_config.inputGains.tone);
    snprintf(e, sizeof(e), "%.2f", current_config.inputGains.analog);
    sendToTeensy(CMD_SET_INPUT_GAINS, a, b, c, d, e);

    // Queue the FIR reload before releasing, so the Teensy sees the load
    // request while still muted and can keep holding across the SD read.
    // Every caller of this function paired it with loadFirFilters() anyway.
    loadFirFilters();

    // Whole preset is now in flight ahead of this in the queue; the Teensy
    // ramps the outputs back up when it reaches this and the FIR load done.
    sendOnOffToTeensy(CMD_SET_CONFIG_HOLD, false);

    // Every full sync is a potential audible-state change (preset switches
    // from the API, button, remote and Teensy reboots all land here)
    compareOnStateChanged();
}

void loadFirFilters() {
    Preset* activePreset = &current_config.presets[current_config.active_preset_index];
    // Stale failures must not outlive the load that caused them; the Teensy
    // re-reports any that still apply as FIRERR lines during this load.
    clearFirLoadErrors();
    // Clients keep their own copy and merge failures into it, so clearing
    // ours is not enough - tell them too. This goes out before the load
    // command, so it always precedes that load's FIRERR lines on the socket.
    broadcastFirPool(*activePreset);
    if (activePreset->firEnabled) {
        sendToTeensy(CMD_LOAD_FIR_FILES, nullptr);
    }
}
