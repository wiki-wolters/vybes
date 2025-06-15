#include "config.h"
#include <string.h>

/**
 * @brief Finds a preset by its name.
 * @param name The name of the preset to find.
 * @return The index of the preset in the current_config.presets array, or -1 if not found.
 */
int find_preset_by_name(const char* name) {
    for (int i = 0; i < MAX_PRESETS; i++) {
        if (strlen(current_config.presets[i].name) > 0 && strcmp(current_config.presets[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Finds the first available (unused) preset slot.
 * @return The index of the empty slot, or -1 if no slots are available.
 */
int find_empty_preset_slot() {
    for (int i = 0; i < MAX_PRESETS; i++) {
        if (strlen(current_config.presets[i].name) == 0) {
            return i;
        }
    }
    return -1;
}
