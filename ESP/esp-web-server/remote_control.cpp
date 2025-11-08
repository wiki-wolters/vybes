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
    PREVIOUS_PRESET
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
    {0xFF02FD, PREVIOUS_PRESET}
};

const char* actionToString(IRACTION action) {
    switch (action) {
        case VOLUME_UP: return "VOLUME_UP";
        case VOLUME_DOWN: return "VOLUME_DOWN";
        case MUTE: return "MUTE";
        case NEXT_PRESET: return "NEXT_PRESET";
        case PREVIOUS_PRESET: return "PREVIOUS_PRESET";
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

    if (_preset_selection_time > 0 && millis() - _preset_selection_time > 1000) {
        apply_preset();
    }
}

void RemoteControl::handle_ir_code(uint64_t code) {
    IRACTION action = getAction(code);
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
            break;
        case VOLUME_DOWN:
            decrease_volume();
            if (millis() - lastVolumeScreenUpdateTime > VOLUME_SCREEN_UPDATE_INTERVAL) {
                writeToScreen("Volume " + String(current_config.volume), 3000);
                lastVolumeScreenUpdateTime = millis();
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
        
        String ws_response;
        serializeJson(doc, ws_response);
        broadcastWebSocket(ws_response);
    }
    _preset_selection_time = 0;
}
