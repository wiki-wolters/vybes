#include <EEPROM.h>
#include "config.h"

// Define the global configuration instance
Config current_config;

void save_config() {
    EEPROM.put(0, current_config);
    EEPROM.commit(); // Write data to flash
}

void load_config() {
    EEPROM.get(0, current_config);
}

void reset_config_to_defaults() {
    Serial.println("Resetting configuration to defaults...");
    
    // Create a new default configuration object
    Config defaultConfig;
    
    // Set the magic value to indicate it's initialized
    defaultConfig.magic_value = CONFIG_MAGIC_VALUE;
    defaultConfig.active_preset_index = 0;

    // Initialize the first preset as 'Default'
    strcpy(defaultConfig.presets[0].name, "Default");
    defaultConfig.presets[0].delay = Delay(); // Initialize with default values
    defaultConfig.presets[0].crossover = Crossover(); // Initialize with default values
    defaultConfig.presets[0].equalLoudness = false;
    // Initialize PEQ sets for the default preset
    for (int j = 0; j < MAX_PEQ_SETS; j++) {
        defaultConfig.presets[0].room_correction[j] = PEQSet();
        defaultConfig.presets[0].preference_curve[j] = PEQSet();
    }

    // Initialize remaining presets as unused
    for (int i = 1; i < MAX_PRESETS; i++) {
        strcpy(defaultConfig.presets[i].name, ""); // Empty name means unused
    }

    // Copy the default config to the global current_config
    current_config = defaultConfig;
    
    // Save this new default configuration to EEPROM
    save_config();
}

void init_config() {
    EEPROM.begin(EEPROM_SIZE);
    
    uint32_t magic; 
    EEPROM.get(0, magic);

    if (magic == CONFIG_MAGIC_VALUE) {
        Serial.println("Found existing configuration in EEPROM. Loading...");
        load_config();
    } else {
        Serial.println("No valid configuration found in EEPROM.");
        reset_config_to_defaults();
    }
}
