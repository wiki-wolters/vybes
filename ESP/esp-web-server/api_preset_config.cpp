#include "globals.h"
#include "web_server.h"
#include "websocket.h"
#include "utilities.h"
#include "teensy_comm.h"
#include "config.h"
#include "api_helpers.h"
#include <string.h>

void handlePostPresetEQ(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    String eqType = request->pathArg(1);
    String splStr = request->pathArg(2);
    int spl = splStr.toInt();

    if (eqType != "roomCorrection" && eqType != "preferenceCurve") {
        request->send(400, "text/plain", "Invalid EQ type");
        return;
    }

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    Preset* preset = &current_config.presets[presetIndex];
    PEQSet* sets = (eqType == "roomCorrection") ? preset->room_correction : preset->preference_curve;

    // Check if SPL already exists
    for (int i = 0; i < MAX_PEQ_SETS; i++) {
        if (sets[i].spl != -1 && sets[i].spl == spl) {
            request->send(409, "text/plain", "SPL value already exists");
            return;
        }
    }

    // Find an empty slot
    int empty_slot = -1;
    for (int i = 0; i < MAX_PEQ_SETS; i++) {
        if (sets[i].spl == -1) {
            empty_slot = i;
            break;
        }
    }

    if (empty_slot == -1) {
        request->send(507, "text/plain", "No available EQ set slots");
        return;
    }

    // Add the new set
    sets[empty_slot].spl = spl;
    sets[empty_slot].num_points = 0; // Initialize with no points

    scheduleConfigWrite();
    request->send(201, "application/json", "{}");
}

void handleDeletePresetEQ(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    String eqType = request->pathArg(1);
    String splStr = request->pathArg(2);
    int spl = splStr.toInt();

    if (eqType != "roomCorrection" && eqType != "preferenceCurve") {
        request->send(400, "text/plain", "Invalid EQ type");
        return;
    }

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    Preset* preset = &current_config.presets[presetIndex];
    PEQSet* sets = (eqType == "roomCorrection") ? preset->room_correction : preset->preference_curve;

    // Find the set with the matching SPL
    int found_index = -1;
    for (int i = 0; i < MAX_PEQ_SETS; i++) {
        if (sets[i].spl == spl) {
            found_index = i;
            break;
        }
    }

    if (found_index == -1) {
        request->send(404, "text/plain", "SPL value not found");
        return;
    }

    // "Delete" the set by resetting its SPL to -1
    sets[found_index].spl = -1;
    sets[found_index].num_points = 0;

    scheduleConfigWrite();
    request->send(204, "");
}

void handlePutPresetCrossover(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    String freqStr = request->pathArg(2);
    int freq = freqStr.toInt();

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    Preset* preset = &current_config.presets[presetIndex];

    preset->crossoverFreq = freq;

    scheduleConfigWrite();
    sendToTeensy("crossoverFreq", freqStr);
    request->send(200, "application/json", "{}");
}

void handlePutPresetCrossoverEnabled(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    String state = request->pathArg(1);
    bool enabled;

    if (state == "on") {
        enabled = true;
    } else if (state == "off") {
        enabled = false;
    } else {
        request->send(400, "text/plain", "Invalid state");
        return;
    }

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    current_config.presets[presetIndex].crossoverEnabled = enabled;

    scheduleConfigWrite();
    sendToTeensy("crossoverEnabled", state);
    request->send(200, "application/json", "{}");
}

void handlePutPresetEQPoints(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (index == 0) {
        // This is the first chunk of data, you might want to allocate memory here
        // and store the request pointer if you need it across multiple chunks.
    }

    // For this implementation, we assume the data fits in a single chunk.
    // For larger data, you would need to buffer it.

    String presetName = request->pathArg(0);
    String eqType = request->pathArg(1);
    String splStr = request->pathArg(2);
    int spl = splStr.toInt();

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    Preset* preset = &current_config.presets[presetIndex];
    PEQSet* sets = (eqType == "roomCorrection") ? preset->room_correction : preset->preference_curve;

    int set_index = -1;
    for (int i = 0; i < MAX_PEQ_SETS; i++) {
        if (sets[i].spl == spl) {
            set_index = i;
            break;
        }
    }

    if (set_index == -1) {
        request->send(404, "text/plain", "EQ set with specified SPL not found");
        return;
    }

    PEQSet* target_set = &sets[set_index];

    DynamicJsonDocument doc(1024); // Adjust size as needed
    DeserializationError error = deserializeJson(doc, (const char*)data, len);

    if (error) {
        request->send(400, "text/plain", "Invalid JSON");
        return;
    }

    JsonArray pointsArray = doc.as<JsonArray>();
    if (pointsArray.size() > MAX_PEQ_POINTS) {
        request->send(400, "text/plain", "Too many PEQ points");
        return;
    }

    target_set->num_points = pointsArray.size();
    int i = 0;
    for (JsonObject point : pointsArray) {
        target_set->points[i].freq = point["freq"].as<float>();
        target_set->points[i].gain = point["gain"].as<float>();
        target_set->points[i].q = point["q"].as<float>();
        i++;
    }

    scheduleConfigWrite();
    request->send(200, "application/json", "{}");
    
    // Send updated PEQ points to Teensy
    String command = "update_eq " + eqType + " " + String(spl);
    sendToTeensy(command.c_str(), "");
}

void handlePutPresetEQEnabled(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    String eqType = request->pathArg(1);
    String state = request->pathArg(2);
    
    if (state != "on" && state != "off") {
        request->send(400, "text/plain", "Invalid state. Must be 'on' or 'off'");
        return;
    }
    
    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }
    
    Preset* preset = &current_config.presets[presetIndex];
    bool enabled = (state == "on");
    
    // In the new structure, we have a single EQEnabled flag that controls both room correction and preference curve
    preset->EQEnabled = enabled;
    
    // The eqType parameter is kept for backward compatibility but we don't distinguish between room/preference anymore
    if (eqType != "room" && eqType != "preference") {
        request->send(400, "text/plain", "Invalid EQ type. Must be 'room' or 'preference'");
        return;
    }
    
    scheduleConfigWrite();
    
    // Send update to Teensy
    String command = String("eq_") + eqType + "_enabled " + (enabled ? "1" : "0");
    sendToTeensy(command.c_str(), "");
    
    DynamicJsonDocument doc(128);
    doc["enabled"] = enabled;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handlePostPresetEQCopy(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    String eqType = request->pathArg(1);
    int sourceSpl = request->pathArg(2).toInt();
    int targetSpl = request->pathArg(3).toInt();
    
    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }
    
    Preset* preset = &current_config.presets[presetIndex];
    PEQSet* sets = (eqType == "room") ? preset->room_correction : preset->preference_curve;
    
    // Find source set
    int sourceIndex = -1;
    for (int i = 0; i < MAX_PEQ_SETS; i++) {
        if (sets[i].spl == sourceSpl) {
            sourceIndex = i;
            break;
        }
    }
    
    if (sourceIndex == -1) {
        request->send(404, "text/plain", "Source SPL not found");
        return;
    }
    
    // Find or create target set
    int targetIndex = -1;
    for (int i = 0; i < MAX_PEQ_SETS; i++) {
        if (sets[i].spl == targetSpl) {
            targetIndex = i;
            break;
        } else if (sets[i].spl == 0 && targetIndex == -1) {
            // Found an empty slot
            targetIndex = i;
        }
    }
    
    if (targetIndex == -1) {
        request->send(507, "text/plain", "No space for new EQ set");
        return;
    }
    
    // Copy the EQ set
    memcpy(&sets[targetIndex], &sets[sourceIndex], sizeof(PEQSet));
    sets[targetIndex].spl = targetSpl;
    
    scheduleConfigWrite();
    
    // Send update to Teensy
    String command = String("copy_eq_") + eqType + " " + String(sourceSpl) + " " + String(targetSpl);
    sendToTeensy(command.c_str(), "");
    
    request->send(200, "application/json", "{}");
}

