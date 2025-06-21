#include "globals.h"
#include "web_server.h"
#include "websocket.h"
#include "utilities.h"
#include "teensy_comm.h"
#include "config.h"
#include "api_helpers.h"
#include <string.h>
#include <ArduinoJson.h>

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

    // Validate frequency range (20Hz to 20kHz)
    if (freq < 20 || freq > 20000) {
        request->send(400, "text/plain", "Crossover frequency must be between 20 and 20000 Hz");
        return;
    }

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    // Update the preset
    current_config.presets[presetIndex].crossoverFreq = freq;
    scheduleConfigWrite();
    
    // Send crossover frequency to Teensy
    sendFloatToTeensy(CMD_SET_CROSSOVER_FREQ, freq);
    
    // Prepare and send response
    DynamicJsonDocument doc(128);
    doc["status"] = "ok";
    doc["crossoverFreq"] = freq;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
    
    // Broadcast update
    broadcastWebSocket(response);
}

void handlePutPresetCrossoverEnabled(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    String state = request->pathArg(1);
    
    if (state != "on" && state != "off") {
        request->send(400, "text/plain", "Invalid state");
        return;
    }
    
    bool enabled = (state == "on");

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    // Update the preset
    current_config.presets[presetIndex].crossoverEnabled = enabled;
    scheduleConfigWrite();
    
    // Send crossover enable/disable to Teensy
    sendOnOffToTeensy(CMD_SET_CROSSOVER_ENABLED, enabled);
    
    // Prepare and send response
    DynamicJsonDocument doc(128);
    doc["status"] = "ok";
    doc["crossoverEnabled"] = enabled;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
    
    // Broadcast update
    broadcastWebSocket(response);
}

void handlePutPresetEQPoints(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
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
    PEQSet* sets = (eqType == "pref") ? preset->preference_curve : preset->room_correction;

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

    /* Identify which points have changed so we only send those to Teensy */
    target_set->num_points = pointsArray.size();
    int i = 0;
    for (JsonObject point : pointsArray) {
        if (point["freq"] != target_set->points[i].freq ||
            point["gain"] != target_set->points[i].gain ||
            point["q"] != target_set->points[i].q
        ) {
            target_set->points[i].freq = point["freq"].as<float>();
            target_set->points[i].gain = point["gain"].as<float>();
            target_set->points[i].q    = point["q"].as<float>();
            String pointData = String(target_set->points[i].freq, 1) + " " +
                             String(target_set->points[i].q, 2) + " " +
                             String(target_set->points[i].gain, 2);
            sendToTeensy(CMD_SET_EQ_FILTER, String(i), pointData);
        }
        i++;
    }

    scheduleConfigWrite();
    
    // Prepare and send response
    doc["status"] = "ok";
    doc["eqType"] = eqType;
    doc["spl"] = spl;
    doc["numPoints"] = target_set->num_points;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
    
    // Broadcast update
    broadcastWebSocket(response);
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
    
    bool enabled = (state == "on");
    
    if (eqType == "pref") {
        current_config.presets[presetIndex].EQEnabled = enabled;
    }
    
    scheduleConfigWrite();
    
    // Send command to Teensy
    if (eqType == "pref") {
        sendOnOffToTeensy(CMD_SET_EQ_ENABLED, enabled);
    }
    
    // Prepare and send response
    DynamicJsonDocument doc(128);
    doc["status"] = "ok";
    doc["eqType"] = eqType;
    doc["enabled"] = enabled;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
    
    // Broadcast update
    broadcastWebSocket(response);
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
    PEQSet* sets = (eqType == "roomCorrection") ? preset->room_correction : preset->preference_curve;
    
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
        } else if (sets[i].spl == -1 && targetIndex == -1) {
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
    
    // Send command to Teensy to copy the EQ set
    sendToTeensy(CMD_SET_EQ_FILTER, "copy", 
                String(sourceSpl) + " " + String(targetSpl), 
                eqType);
    
    // Prepare and send response
    DynamicJsonDocument doc(128);
    doc["status"] = "ok";
    doc["eqType"] = eqType;
    doc["sourceSpl"] = sourceSpl;
    doc["targetSpl"] = targetSpl;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
    
    // Broadcast update
    broadcastWebSocket(response);
}

void handleResetEQFilters(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    String eqType = request->pathArg(1);
    
    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }
    
    Preset* preset = &current_config.presets[presetIndex];
    
    // Reset the specified EQ type or both if not specified
    if (eqType == "room" || eqType == "both") {
        // Reset room correction
        for (int i = 0; i < MAX_PEQ_SETS; i++) {
            preset->room_correction[i].spl = -1;
            preset->room_correction[i].num_points = 0;
        }
        // Add default set at 0dB SPL
        preset->room_correction[0].spl = 0;
        preset->room_correction[0].num_points = 0;
    }
    
    if (eqType == "preference" || eqType == "both") {
        // Reset preference curve
        for (int i = 0; i < MAX_PEQ_SETS; i++) {
            preset->preference_curve[i].spl = -1;
            preset->preference_curve[i].num_points = 0;
        }
        // Add default set at 0dB SPL
        preset->preference_curve[0].spl = 0;
        preset->preference_curve[0].num_points = 0;
    }
    
    scheduleConfigWrite();
    
    // Send command to Teensy to reset EQ filters
    sendToTeensy(CMD_RESET_EQ_FILTERS, eqType);
    
    // Prepare and send response
    DynamicJsonDocument doc(128);
    doc["status"] = "ok";
    doc["eqType"] = eqType;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
    
    // Broadcast update
    broadcastWebSocket(response);
}

