#include "config.h"
#include "teensy_comm.h"
#include "screen.h"

// Define the global configuration instance
Config current_config;

void save_config() {
    //TODO: save config to LittleFS
}

void reset_config_to_defaults() {
    Serial.println("Resetting configuration to defaults...");
    
    current_config.active_preset_index = 0;

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
    //Set first 3 default points
    for (int k = 0; k < 3; k++) {
        current_config.presets[0].preference_curve[0].points[k] = PEQPoint();
        current_config.presets[0].preference_curve[0].points[k].freq = 100 * pow(10, k);
    }
    current_config.presets[0].preference_curve[0].num_points = 3;

    // Initialize remaining presets as unused
    for (int i = 1; i < MAX_PRESETS; i++) {
        strcpy(current_config.presets[i].name, ""); // Empty name means unused
    }

    // Save this new default configuration
    //save_config();

    updateTeensyWithActivePresetParameters();
}

void init_config() {
    reset_config_to_defaults();
}

void nextPreset() {
    int8_t nextPresetIndex = current_config.active_preset_index + 1;
    if (nextPresetIndex >= MAX_PRESETS || strlen(current_config.presets[nextPresetIndex].name) == 0) {
        nextPresetIndex = 0;
    }
    current_config.active_preset_index = nextPresetIndex;
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
    
    // Send FIR filter settings
    sendOnOffToTeensy(CMD_SET_FIR_ENABLED, activePreset->FIRFiltersEnabled);
    if (activePreset->FIRFiltersEnabled) {
        sendToTeensy(CMD_SET_FIR, "left", activePreset->FIRFilters.left);
        sendToTeensy(CMD_SET_FIR, "right", activePreset->FIRFilters.right);
        sendToTeensy(CMD_SET_FIR, "sub", activePreset->FIRFilters.sub);
    }
}
