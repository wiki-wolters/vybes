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

    doc["isCurrent"] = presetIndex == current_config.active_preset_index;
    
    // Add delay information
    JsonObject speakerDelays = doc.createNestedObject("speakerDelays");
    speakerDelays["left"] = preset.delay.left;
    speakerDelays["right"] = preset.delay.right;
    speakerDelays["sub"] = preset.delay.sub;
    doc["isSpeakerDelayEnabled"] = preset.delayEnabled;

    // Add crossover information
    doc["crossoverFreq"] = preset.crossoverFreq;
    doc["isCrossoverEnabled"] = preset.crossoverEnabled;

    // Add EQ and FIR filter states
    doc["isFIREnabled"] = preset.FIRFiltersEnabled;

    //Fir filenames
    JsonArray firFiles = doc.createNestedArray("firFiles");
    // TODO: load fir files from Teensy and populate
    
    // Add FIR filter filenames
    doc["firLeft"] = preset.FIRFilters.left;
    doc["firRight"] = preset.FIRFilters.right;
    doc["firSub"] = preset.FIRFilters.sub;

    doc["isPreferenceEQEnabled"] = preset.EQEnabled;
    JsonArray preferenceCurve = doc.createNestedArray("preferenceEQ");
    for(int i=0; i < MAX_PEQ_SETS; i++) {
        if(preset.preference_curve[i].spl != -1) {
            JsonObject peqSet = preferenceCurve.createNestedObject();
            peqSet["spl"] = preset.preference_curve[i].spl;
            JsonArray peqs = peqSet.createNestedArray("peqs");
            for(int j=0; j < preset.preference_curve[i].num_points; j++) {
                    JsonObject peq = peqs.createNestedObject();
                    peq["freq"] = preset.preference_curve[i].points[j].freq;
                    peq["gain"] = preset.preference_curve[i].points[j].gain;
                    peq["q"] = preset.preference_curve[i].points[j].q;
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
        request->send(400, "text/plain", "Preset name must be between 1 and " + String(PRESET_NAME_MAX_LEN) + " characters");
        return;
    }

    if (find_preset_by_name(presetName.c_str()) != -1) {
        request->send(409, "text/plain", "Preset name already exists");
        return;
    }

    int newIndex = find_empty_prese t_slot();
    if (newIndex == -1) {
        request->send(507, "text/plain", "Maximum number of presets reached");
        return;
    }

    // Create new preset with default values
    current_config.presets[newIndex] = Preset();
    strcpy(current_config.presets[newIndex].name, presetName.c_str());
    
    // Initialize delay and crossover settings
    current_config.presets[newIndex].delayEnabled = false;
    current_config.presets[newIndex].crossoverFreq = 80;
    current_config.presets[newIndex].crossoverEnabled = false;
    current_config.presets[newIndex].EQEnabled = false;
    current_config.presets[newIndex].FIRFiltersEnabled = false;
    
    // Initialize the first PEQ set for both curves as this is what the UI expects
    current_config.presets[newIndex].room_correction[0].spl = 0;
    current_config.presets[newIndex].room_correction[0].num_points = 0;
    current_config.presets[newIndex].preference_curve[0].spl = 0;
    current_config.presets[newIndex].preference_curve[0].num_points = 0;

    scheduleConfigWrite();
    request->send(201, "application/json", "{}");
}

void handlePostPresetCopy(AsyncWebServerRequest *request) {
    String sourceName = request->pathArg(0);
    String destName = request->pathArg(1);

    if (destName.length() == 0 || destName.length() >= PRESET_NAME_MAX_LEN) {
        request->send(400, "text/plain", "Destination preset name must be between 1 and " + String(PRESET_NAME_MAX_LEN) + " characters");
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
    scheduleConfigWrite();

    request->send(200, "application/json", "{}"); // HTTP response

    // Prepare data for WebSocket broadcast
    DynamicJsonDocument doc(256);
    doc["messageType"] = "activePresetChanged";
    doc["activePresetName"] = current_config.presets[current_config.active_preset_index].name;
    doc["activePresetIndex"] = current_config.active_preset_index;
    
    String ws_response;
    serializeJson(doc, ws_response);

    broadcastWebSocket(ws_response);
}

void handlePutPresetDelayEnabled(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    String state = request->pathArg(1);

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    current_config.presets[presetIndex].delayEnabled = (state == "true");
    scheduleConfigWrite();

    request->send(200, "application/json", "{}");
}

void handlePutPresetDelayNamed(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    String speaker = request->pathArg(1);
    String delay = request->pathArg(2);

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    if (speaker == "left") {
        current_config.presets[presetIndex].delay.left = delay.toFloat();
    } else if (speaker == "right") {
        current_config.presets[presetIndex].delay.right = delay.toFloat();
    } else if (speaker == "sub") {
        current_config.presets[presetIndex].delay.sub = delay.toFloat();
    }
    scheduleConfigWrite();

    request->send(200, "application/json", "{}");
}

