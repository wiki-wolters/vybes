#include "config.h"
#include "teensy_comm.h"
#include "screen.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

// Define the global configuration instance
Config current_config;

const char* CONFIG_FILE = "/config.msgpack";

bool load_config() {
    if (!LittleFS.exists(CONFIG_FILE)) {
        Serial.println("Config file not found, using defaults");
        return false;
    }

    File file = LittleFS.open(CONFIG_FILE, "r");
    if (!file) {
        Serial.println("Failed to open config file for reading");
        return false;
    }

    // Create a buffer to hold the MessagePack data
    size_t fileSize = file.size();
    if (fileSize == 0) {
        Serial.println("Config file is empty");
        file.close();
        return false;
    }

    std::unique_ptr<char[]> buffer(new char[fileSize]);
    file.readBytes(buffer.get(), fileSize);
    file.close();

    // Deserialize MessagePack
    DynamicJsonDocument doc(8192); // Adjust size as needed
    DeserializationError error = deserializeMsgPack(doc, buffer.get(), fileSize);
    
    if (error) {
        Serial.print("Failed to deserialize config: ");
        Serial.println(error.c_str());
        return false;
    }

    // Load version and check compatibility
    uint8_t file_version = doc["version"] | 1; // Default to version 1 if not present
    if (file_version > CONFIG_CURRENT_VERSION) {
        Serial.println("Config file version is newer than supported, using defaults");
        return false;
    }

    // Load global settings
    current_config.version = CONFIG_CURRENT_VERSION;
    current_config.active_preset_index = doc["active_preset_index"] | 0;
    current_config.toneFrequency = doc["toneFrequency"] | 0;
    current_config.toneVolume = doc["toneVolume"] | 0;
    current_config.noiseVolume = doc["noiseVolume"] | 0;
    current_config.muted = doc["muted"] | false;
    current_config.mutePercent = doc["mutePercent"] | 0;
    current_config.volume = doc["volume"] | 50;

    // Load speaker gains
    if (doc.containsKey("speakerGains")) {
        JsonObject gains = doc["speakerGains"];
        current_config.speakerGains.left = gains["left"] | 1.0f;
        current_config.speakerGains.right = gains["right"] | 1.0f;
        current_config.speakerGains.sub = gains["sub"] | 1.0f;
    }

    // Load input gains
    if (doc.containsKey("inputGains")) {
        JsonObject gains = doc["inputGains"];
        current_config.inputGains.spdif = gains["spdif"] | 1.0f;
        current_config.inputGains.bluetooth = gains["bluetooth"] | 1.0f;
    }

    // Load presets
    if (doc.containsKey("presets")) {
        JsonArray presets = doc["presets"];
        for (int i = 0; i < MAX_PRESETS && i < presets.size(); i++) {
            JsonObject preset = presets[i];
            
            // Load preset name
            const char* name = preset["name"];
            if (name) {
                strncpy(current_config.presets[i].name, name, PRESET_NAME_MAX_LEN - 1);
                current_config.presets[i].name[PRESET_NAME_MAX_LEN - 1] = '\0';
            } else {
                strcpy(current_config.presets[i].name, "");
            }

            // Load other preset data only if the preset is in use
            if (strlen(current_config.presets[i].name) > 0) {
                // Load delay settings
                if (preset.containsKey("delay")) {
                    JsonObject delay = preset["delay"];
                    current_config.presets[i].delay.left = delay["left"] | 0.0f;
                    current_config.presets[i].delay.right = delay["right"] | 0.0f;
                    current_config.presets[i].delay.sub = delay["sub"] | 0.0f;
                }
                current_config.presets[i].delayEnabled = preset["delayEnabled"] | false;

                // Load crossover settings
                current_config.presets[i].crossoverFreq = preset["crossoverFreq"] | 80;
                current_config.presets[i].crossoverEnabled = preset["crossoverEnabled"] | false;

                // Load EQ settings
                current_config.presets[i].EQEnabled = preset["EQEnabled"] | false;
                
                // Load preference curve (PEQ sets)
                if (preset.containsKey("preference_curve")) {
                    JsonArray peqSets = preset["preference_curve"];
                    for (int j = 0; j < MAX_PEQ_SETS && j < peqSets.size(); j++) {
                        JsonObject peqSet = peqSets[j];
                        current_config.presets[i].preference_curve[j].spl = peqSet["spl"] | 0;
                        current_config.presets[i].preference_curve[j].num_points = peqSet["num_points"] | 0;
                        
                        if (peqSet.containsKey("points")) {
                            JsonArray points = peqSet["points"];
                            for (int k = 0; k < MAX_PEQ_POINTS && k < points.size(); k++) {
                                JsonObject point = points[k];
                                current_config.presets[i].preference_curve[j].points[k].freq = point["freq"] | 1000.0f;
                                current_config.presets[i].preference_curve[j].points[k].gain = point["gain"] | 0.0f;
                                current_config.presets[i].preference_curve[j].points[k].q = point["q"] | 1.0f;
                            }
                        }
                    }
                }

                // Load FIR filter settings
                current_config.presets[i].FIRFiltersEnabled = preset["FIRFiltersEnabled"] | false;
                if (preset.containsKey("FIRFilters")) {
                    JsonObject firFilters = preset["FIRFilters"];
                    current_config.presets[i].FIRFilters.left = firFilters["left"] | "";
                    current_config.presets[i].FIRFilters.right = firFilters["right"] | "";
                    current_config.presets[i].FIRFilters.sub = firFilters["sub"] | "";
                }
            }
        }
    }

    Serial.println("Config loaded successfully");
    return true;
}

void save_config() {
    Serial.println("Saving configuration to LittleFS...");
    
    // Create JSON document
    DynamicJsonDocument doc(8192); // Adjust size as needed
    
    // Save version and global settings
    doc["version"] = current_config.version;
    doc["active_preset_index"] = current_config.active_preset_index;
    doc["toneFrequency"] = current_config.toneFrequency;
    doc["toneVolume"] = current_config.toneVolume;
    doc["noiseVolume"] = current_config.noiseVolume;
    doc["muted"] = current_config.muted;
    doc["mutePercent"] = current_config.mutePercent;
    doc["volume"] = current_config.volume;

    // Save speaker gains
    JsonObject speakerGains = doc.createNestedObject("speakerGains");
    speakerGains["left"] = current_config.speakerGains.left;
    speakerGains["right"] = current_config.speakerGains.right;
    speakerGains["sub"] = current_config.speakerGains.sub;

    // Save input gains
    JsonObject inputGains = doc.createNestedObject("inputGains");
    inputGains["spdif"] = current_config.inputGains.spdif;
    inputGains["bluetooth"] = current_config.inputGains.bluetooth;

    // Save presets
    JsonArray presets = doc.createNestedArray("presets");
    for (int i = 0; i < MAX_PRESETS; i++) {
        JsonObject preset = presets.createNestedObject();
        preset["name"] = current_config.presets[i].name;

        // Only save the rest of the data if the preset is in use
        if (strlen(current_config.presets[i].name) > 0) {
            // Save delay settings
            JsonObject delay = preset.createNestedObject("delay");
            delay["left"] = current_config.presets[i].delay.left;
            delay["right"] = current_config.presets[i].delay.right;
            delay["sub"] = current_config.presets[i].delay.sub;
            preset["delayEnabled"] = current_config.presets[i].delayEnabled;
            
            // Save crossover settings
            preset["crossoverFreq"] = current_config.presets[i].crossoverFreq;
            preset["crossoverEnabled"] = current_config.presets[i].crossoverEnabled;
            
            // Save EQ settings
            preset["EQEnabled"] = current_config.presets[i].EQEnabled;
            
            // Save preference curve (PEQ sets)
            JsonArray peqSets = preset.createNestedArray("preference_curve");
            for (int j = 0; j < MAX_PEQ_SETS; j++) {
                if (current_config.presets[i].preference_curve[j].spl != -1) {
                    JsonObject peqSet = peqSets.createNestedObject();
                    peqSet["spl"] = current_config.presets[i].preference_curve[j].spl;
                    peqSet["num_points"] = current_config.presets[i].preference_curve[j].num_points;
                    
                    JsonArray points = peqSet.createNestedArray("points");
                    for (int k = 0; k < current_config.presets[i].preference_curve[j].num_points; k++) {
                        JsonObject point = points.createNestedObject();
                        point["freq"] = current_config.presets[i].preference_curve[j].points[k].freq;
                        point["gain"] = current_config.presets[i].preference_curve[j].points[k].gain;
                        point["q"] = current_config.presets[i].preference_curve[j].points[k].q;
                    }
                }
            }
            
            // Save FIR filter settings
            preset["FIRFiltersEnabled"] = current_config.presets[i].FIRFiltersEnabled;
            JsonObject firFilters = preset.createNestedObject("FIRFilters");
            firFilters["left"] = current_config.presets[i].FIRFilters.left;
            firFilters["right"] = current_config.presets[i].FIRFilters.right;
            firFilters["sub"] = current_config.presets[i].FIRFilters.sub;
        }
    }

    // Serialize to MessagePack
    File file = LittleFS.open(CONFIG_FILE, "w");
    if (!file) {
        Serial.println("Failed to open config file for writing");
        return;
    }

    size_t bytesWritten = serializeMsgPack(doc, file);
    file.close();
    
    if (bytesWritten > 0) {
        Serial.print("Config saved successfully, ");
        Serial.print(bytesWritten);
        Serial.println(" bytes written");
    } else {
        Serial.println("Failed to write config file");
    }
}

void reset_config_to_defaults() {
    Serial.println("Resetting configuration to defaults...");
    
    current_config.version = CONFIG_CURRENT_VERSION;
    current_config.active_preset_index = 0;
    current_config.toneFrequency = 0;
    current_config.toneVolume = 0;
    current_config.noiseVolume = 0;
    current_config.muted = false;
    current_config.mutePercent = 0;
    current_config.volume = 50;
    
    // Reset gains to defaults
    current_config.speakerGains.left = 1.0f;
    current_config.speakerGains.right = 1.0f;
    current_config.speakerGains.sub = 1.0f;
    current_config.inputGains.spdif = 1.0f;
    current_config.inputGains.bluetooth = 1.0f;

    // Initialize the first preset as 'Default'
    strcpy(current_config.presets[0].name, "Default");
    current_config.presets[0].delay = Delay(); // Initialize with default values
    current_config.presets[0].delayEnabled = false;
    current_config.presets[0].crossoverFreq = 80;
    current_config.presets[0].crossoverEnabled = false;
    current_config.presets[0].EQEnabled = false;
    current_config.presets[0].FIRFiltersEnabled = false;
    current_config.presets[0].FIRFilters = FIRFilter();
    
    // Initialize PEQ sets for the default preset
    for (int j = 0; j < MAX_PEQ_SETS; j++) {
        current_config.presets[0].preference_curve[j] = PEQSet();
    }
    
    // Set first 3 default points
    for (int k = 0; k < 3; k++) {
        current_config.presets[0].preference_curve[0].points[k] = PEQPoint();
        current_config.presets[0].preference_curve[0].points[k].freq = 100 * pow(10, k);
    }
    current_config.presets[0].preference_curve[0].num_points = 3;

    // Initialize remaining presets as unused
    for (int i = 1; i < MAX_PRESETS; i++) {
        strcpy(current_config.presets[i].name, ""); // Empty name means unused
    }
}

void init_config() {
    // Initialize LittleFS
    if (!LittleFS.begin()) {
        Serial.println("Failed to initialize LittleFS");
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



void updateTeensyWithActivePresetParameters() {
    Preset* activePreset = &current_config.presets[current_config.active_preset_index];

    //Update displayed preset name
    Serial.print("Updating screen: ");Serial.print(current_config.active_preset_index);Serial.print(" ");Serial.println(activePreset->name);
    writeToScreen(activePreset->name);

    // Send delay settings
    sendToTeensy(CMD_SET_DELAYS, 
        String((int)activePreset->delay.left),
        String((int)activePreset->delay.right),
        String((int)activePreset->delay.sub));
    sendOnOffToTeensy(CMD_SET_DELAY_ENABLED, activePreset->delayEnabled);
    
    // Send crossover settings
    sendFloatToTeensy(CMD_SET_CROSSOVER_FREQ, activePreset->crossoverFreq);
    sendOnOffToTeensy(CMD_SET_CROSSOVER_ENABLED, activePreset->crossoverEnabled);
    
    // Send EQ settings
    sendOnOffToTeensy(CMD_SET_EQ_ENABLED, activePreset->EQEnabled);
    if (activePreset->EQEnabled) {
        // Send preference curve EQ points for all SPL sets
        for (int i = 0; i < MAX_PEQ_SETS; i++) {
            if (activePreset->preference_curve[i].spl != -1) {
                // Send all EQ points for this SPL set
                for (int j = 0; j < activePreset->preference_curve[i].num_points; j++) {
                    String pointData = String(activePreset->preference_curve[i].points[j].freq, 1) + " " +
                                     String(activePreset->preference_curve[i].points[j].q, 2) + " " +
                                     String(activePreset->preference_curve[i].points[j].gain, 2);
                    sendToTeensy(CMD_SET_EQ_FILTER, String(j), pointData);
                }
            }
        }
    }
    
    // Send volume
    sendFloatToTeensy(CMD_SET_VOLUME, current_config.volume / 100.0f);

    // Send FIR filter settings
    sendOnOffToTeensy(CMD_SET_FIR_ENABLED, activePreset->FIRFiltersEnabled);
    sendToTeensy(CMD_SET_FIR, "left", activePreset->FIRFilters.left);
    sendToTeensy(CMD_SET_FIR, "right", activePreset->FIRFilters.right);
    sendToTeensy(CMD_SET_FIR, "sub", activePreset->FIRFilters.sub);

    if (activePreset->FIRFiltersEnabled) {
        sendToTeensy(CMD_LOAD_FIR_FILES);
    }
}