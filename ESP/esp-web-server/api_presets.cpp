#include "globals.h"
#include "web_server.h"
#include "websocket.h"
#include "utilities.h"
#include "config.h"
#include "api_helpers.h"
#include <ArduinoJson.h>
#include <string.h>

// --- API Handlers ---

void handleGetPresets(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(1024);
    JsonArray presets = doc.to<JsonArray>();
    for (int i = 0; i < MAX_PRESETS; i++) {
        if (strlen(current_config.presets[i].name) > 0) {
            presets.add(current_config.presets[i].name);
        }
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleGetPreset(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    int presetIndex = find_preset_by_name(presetName.c_str());

    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    const Preset& preset = current_config.presets[presetIndex];
    DynamicJsonDocument doc(3072); // Increased size for complex presets
    doc["name"] = preset.name;
    
    JsonObject delay = doc.createNestedObject("delay");
    delay["left"] = preset.delay.left;
    delay["right"] = preset.delay.right;
    delay["sub"] = preset.delay.sub;

    JsonObject crossover = doc.createNestedObject("crossover");
    crossover["lowPass"] = preset.crossover.lowPass;
    crossover["highPass"] = preset.crossover.highPass;

    doc["equalLoudness"] = preset.equalLoudness;

    JsonArray roomCorrection = doc.createNestedArray("roomCorrection");
    for(int i=0; i < MAX_PEQ_SETS; i++) {
        if(preset.room_correction[i].enabled) {
            JsonObject peqSet = roomCorrection.createNestedObject();
            peqSet["spl"] = preset.room_correction[i].spl;
            JsonArray points = peqSet.createNestedArray("points");
            for(int j=0; j < MAX_PEQ_POINTS; j++) {
                if(preset.room_correction[i].points[j].enabled) {
                    JsonObject point = points.createNestedObject();
                    point["freq"] = preset.room_correction[i].points[j].freq;
                    point["gain"] = preset.room_correction[i].points[j].gain;
                    point["q"] = preset.room_correction[i].points[j].q;
                }
            }
        }
    }

    JsonArray preferenceCurve = doc.createNestedArray("preferenceCurve");
    for(int i=0; i < MAX_PEQ_SETS; i++) {
        if(preset.preference_curve[i].enabled) {
            JsonObject peqSet = preferenceCurve.createNestedObject();
            peqSet["spl"] = preset.preference_curve[i].spl;
            JsonArray points = peqSet.createNestedArray("points");
            for(int j=0; j < MAX_PEQ_POINTS; j++) {
                if(preset.preference_curve[i].points[j].enabled) {
                    JsonObject point = points.createNestedObject();
                    point["freq"] = preset.preference_curve[i].points[j].freq;
                    point["gain"] = preset.preference_curve[i].points[j].gain;
                    point["q"] = preset.preference_curve[i].points[j].q;
                }
            }
        }
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handlePostPresetCreate(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);

    if (presetName.length() == 0 || presetName.length() >= PRESET_NAME_MAX_LEN) {
        request->send(400, "text/plain", "Invalid preset name");
        return;
    }

    if (find_preset_by_name(presetName.c_str()) != -1) {
        request->send(409, "text/plain", "Preset name already exists");
        return;
    }

    int newIndex = find_empty_preset_slot();
    if (newIndex == -1) {
        request->send(507, "text/plain", "Maximum number of presets reached");
        return;
    }

    // Create new preset with default values
    current_config.presets[newIndex] = Preset();
    strcpy(current_config.presets[newIndex].name, presetName.c_str());
    
    // Specifically initialize the first PEQ set for both curves as this is what the UI expects
    current_config.presets[newIndex].room_correction[0].enabled = true;
    current_config.presets[newIndex].room_correction[0].spl = 0;
    current_config.presets[newIndex].preference_curve[0].enabled = true;
    current_config.presets[newIndex].preference_curve[0].spl = 0;

    scheduleConfigWrite();
    request->send(201, "application/json", "{}");
}

void handlePostPresetCopy(AsyncWebServerRequest *request) {
    String sourceName = request->pathArg(0);
    String destName = request->pathArg(1);

    if (destName.length() == 0 || destName.length() >= PRESET_NAME_MAX_LEN) {
        request->send(400, "text/plain", "Invalid destination preset name");
        return;
    }

    if (find_preset_by_name(destName.c_str()) != -1) {
        request->send(409, "text/plain", "Destination preset name already exists");
        return;
    }

    int sourceIndex = find_preset_by_name(sourceName.c_str());
    if (sourceIndex == -1) {
        request->send(404, "text/plain", "Source preset not found");
        return;
    }

    int destIndex = find_empty_preset_slot();
    if (destIndex == -1) {
        request->send(507, "text/plain", "Maximum number of presets reached");
        return;
    }

    // Copy the preset struct
    current_config.presets[destIndex] = current_config.presets[sourceIndex];
    // Update the name
    strcpy(current_config.presets[destIndex].name, destName.c_str());

    scheduleConfigWrite();
    request->send(201, "application/json", "{}");
}

void handlePutPresetRename(AsyncWebServerRequest *request) {
    String oldName = request->pathArg(0);
    String newName = request->pathArg(1);

    if (newName.length() == 0 || newName.length() >= PRESET_NAME_MAX_LEN) {
        request->send(400, "text/plain", "Invalid new preset name");
        return;
    }

    if (find_preset_by_name(newName.c_str()) != -1) {
        request->send(409, "text/plain", "New preset name already exists");
        return;
    }

    int presetIndex = find_preset_by_name(oldName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset to rename not found");
        return;
    }

    // Update name in config
    strcpy(current_config.presets[presetIndex].name, newName.c_str());
    scheduleConfigWrite();

    request->send(200, "application/json", "{}");
}

void handleDeletePreset(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);

    if (presetName == "Default") {
        request->send(400, "text/plain", "Cannot delete the default preset");
        return;
    }

    int presetIndex = find_preset_by_name(presetName.c_str());

    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    // "Delete" by clearing the name, making the slot available
    current_config.presets[presetIndex].name[0] = '\0';

    // If the deleted preset was the active one, switch to the default preset
    if (current_config.active_preset_index == presetIndex) {
        current_config.active_preset_index = 0; // Default is always at index 0
    }

    scheduleConfigWrite();
    request->send(200, "application/json", "{}");
}

void handlePutActivePreset(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    int presetIndex = find_preset_by_name(presetName.c_str());

    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    current_config.active_preset_index = presetIndex;
    strncpy(current_config.currentPresetName, current_config.presets[presetIndex].name, PRESET_NAME_MAX_LEN - 1);
    current_config.currentPresetName[PRESET_NAME_MAX_LEN - 1] = '\0'; // Ensure null termination
    scheduleConfigWrite();

    // Here you would typically send the new active preset data to the DSP
    // sendPresetToTeensy(current_config.presets[presetIndex]);

    request->send(200, "application/json", "{}"); // HTTP response

    // Prepare data for WebSocket broadcast
    DynamicJsonDocument doc(256);
    doc["messageType"] = "activePresetChanged";
    doc["activePresetName"] = current_config.currentPresetName;
    doc["activePresetIndex"] = current_config.active_preset_index;
    
    String ws_response;
    serializeJson(doc, ws_response);

    broadcastWebSocket(ws_response);
}
