#include "remote_control.h"
#include "config.h"
#include "screen.h"
#include "api_volume.h"
#include "api_presets.h"
#include "utilities.h"
#include "websocket.h"
#include <ArduinoJson.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>

const uint16_t kIrLed = D3;
IRrecv irrecv(kIrLed);
decode_results results;

// Define the actions that can be triggered by the remote control
enum IRACTION {
    NONE,
    VOLUME_UP,
    VOLUME_DOWN,
    MUTE,
    NEXT_PRESET,
    PREVIOUS_PRESET,
    VOLUME_REPEAT
};

// Define the structure for a remote control code
struct IRCode {
    uint64_t code;
    IRACTION action;
};

// Define the remote control codes
const IRCode remoteCodes[] = {
    {0xFF906F, VOLUME_UP},
    {0xFFA857, VOLUME_DOWN},
    {0xFFE21D, MUTE},
    {0xFFC23D, NEXT_PRESET},
    {0xFF02FD, PREVIOUS_PRESET},
    {0xFFFFFFFFFFFFFFFF, VOLUME_REPEAT}
};

const char* actionToString(IRACTION action) {
    switch (action) {
        case VOLUME_UP: return "VOLUME_UP";
        case VOLUME_DOWN: return "VOLUME_DOWN";
        case MUTE: return "MUTE";
        case NEXT_PRESET: return "NEXT_PRESET";
        case PREVIOUS_PRESET: return "PREVIOUS_PRESET";
        case VOLUME_REPEAT: return "VOLUME_REPEAT";
        case NONE: return "NONE";
        default: return "UNKNOWN";
    }
}

unsigned long lastVolumeScreenUpdateTime = 0;
const unsigned long VOLUME_SCREEN_UPDATE_INTERVAL = 200; // milliseconds

// Function to get the action for a given IR code
IRACTION getAction(uint64_t code) {
    for (unsigned int i = 0; i < sizeof(remoteCodes) / sizeof(IRCode); i++) {
        if (remoteCodes[i].code == code) {
            return remoteCodes[i].action;
        }
    }
    return NONE;
}

void RemoteControl::setup() {
    irrecv.enableIRIn();  // Start the receiver
    _selected_preset_index = current_config.active_preset_index;
}

void RemoteControl::loop() {
    if (irrecv.decode(&results)) {
        handle_ir_code(results.value);
        irrecv.resume();  // Receive the next value
    }

    if (_volume_button_held_time > 0 && millis() - _last_ir_code_time > 250) {
        _volume_button_held_time = 0;
        _last_volume_action = -1;
    }

    // Volume button held logic
    const unsigned long HOLD_START_TIME = 300; // ms to wait before starting repeat
    const unsigned long RAMP_DURATION = 2000; // ms for ramp up
    const int START_RATE = 3; // increments per second
    const int END_RATE = 6; // increments per second
    const int START_DELAY = 1000 / START_RATE; // 333ms
    const int END_DELAY = 1000 / END_RATE; // 166ms
    const int DELAY_RANGE = START_DELAY - END_DELAY;

    if (_volume_button_held_time > 0) { // if a volume button is being held
        unsigned long held_duration = millis() - _volume_button_held_time;

        if (held_duration > HOLD_START_TIME) {
            float ramp_progress = (float)min((unsigned long)held_duration - HOLD_START_TIME, RAMP_DURATION) / RAMP_DURATION;
            int current_delay = START_DELAY - (int)(DELAY_RANGE * ramp_progress);

            if (millis() - _last_volume_increment_time > current_delay) {
                if (_last_volume_action == VOLUME_UP) {
                    increase_volume(2);
                    if (millis() - lastVolumeScreenUpdateTime > VOLUME_SCREEN_UPDATE_INTERVAL) {
                        writeToScreen("Volume " + String(current_config.volume), 3000);
                        lastVolumeScreenUpdateTime = millis();
                    }
                } else if (_last_volume_action == VOLUME_DOWN) {
                    decrease_volume(2);
                    if (millis() - lastVolumeScreenUpdateTime > VOLUME_SCREEN_UPDATE_INTERVAL) {
                        writeToScreen("Volume " + String(current_config.volume), 3000);
                        lastVolumeScreenUpdateTime = millis();
                    }
                }
                _last_volume_increment_time = millis();
            }
        }
    }


    if (_preset_selection_time > 0 && millis() - _preset_selection_time > 1000) {
        apply_preset();
    }
}

void RemoteControl::handle_ir_code(uint64_t code) {
    IRACTION action = getAction(code);
    _last_ir_code_time = millis();
    Serial.print("Received IR code: ");
    serialPrintUint64(code, HEX);
    Serial.print(", Action: ");
    Serial.println(actionToString(action));

    switch (action) {
        case VOLUME_UP:
            increase_volume();
            if (millis() - lastVolumeScreenUpdateTime > VOLUME_SCREEN_UPDATE_INTERVAL) {
                writeToScreen("Volume " + String(current_config.volume), 3000);
                lastVolumeScreenUpdateTime = millis();
            }
            _last_volume_action = VOLUME_UP;
            _volume_button_held_time = millis();
            _last_volume_increment_time = millis();
            break;
        case VOLUME_DOWN:
            decrease_volume();
            if (millis() - lastVolumeScreenUpdateTime > VOLUME_SCREEN_UPDATE_INTERVAL) {
                writeToScreen("Volume " + String(current_config.volume), 3000);
                lastVolumeScreenUpdateTime = millis();
            }
            _last_volume_action = VOLUME_DOWN;
            _volume_button_held_time = millis();
            _last_volume_increment_time = millis();
            break;
        case VOLUME_REPEAT:
            if (_last_volume_action != VOLUME_UP && _last_volume_action != VOLUME_DOWN) {
                _volume_button_held_time = 0;
                _last_volume_action = -1;
            }
            break;
        case MUTE:
            toggle_mute();
            if (current_config.muted) {
                writeToScreen("Mute On", 3000);
            } else {
                writeToScreen("Mute Off", 3000);
            }
            break;
        case NEXT_PRESET:
            next_preset();
            break;
        case PREVIOUS_PRESET:
            previous_preset();
            break;
        case NONE:
            break;
    }
}

void RemoteControl::next_preset() {
    _selected_preset_index++;
    if (_selected_preset_index >= MAX_PRESETS || strlen(current_config.presets[_selected_preset_index].name) == 0) {
        _selected_preset_index = 0;
    }
    writeToScreen(current_config.presets[_selected_preset_index].name);
    _preset_selection_time = millis();
}

void RemoteControl::previous_preset() {
    _selected_preset_index--;
    if (_selected_preset_index < 0) {
        _selected_preset_index = MAX_PRESETS - 1;
    }
    while (strlen(current_config.presets[_selected_preset_index].name) == 0) {
        _selected_preset_index--;
        if (_selected_preset_index < 0) {
            _selected_preset_index = MAX_PRESETS - 1;
        }
    }
    writeToScreen(current_config.presets[_selected_preset_index].name);
    _preset_selection_time = millis();
}

void RemoteControl::apply_preset() {
    if (_selected_preset_index != current_config.active_preset_index) {
        current_config.active_preset_index = _selected_preset_index;
        updateTeensyWithActivePresetParameters();
        loadFirFilters();
        scheduleConfigWrite();

        // Prepare data for WebSocket broadcast
        DynamicJsonDocument doc(1024);
        doc["messageType"] = "activePresetChanged";
        doc["activePresetName"] = current_config.presets[current_config.active_preset_index].name;
        doc["activePresetIndex"] = current_config.active_preset_index;
        
        char ws_response_buffer[1024]; // Adjust size as needed
        size_t len = serializeJson(doc, ws_response_buffer, sizeof(ws_response_buffer));
        if (len > 0 && len < sizeof(ws_response_buffer)) {
            broadcastWebSocket(ws_response_buffer);
        } else {
            Serial.println("Error serializing JSON for WebSocket broadcast or buffer too small.");
        }
    }
    _preset_selection_time = 0;
}