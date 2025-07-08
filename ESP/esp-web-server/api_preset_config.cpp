#include "globals.h"
#include "web_server.h"
#include "websocket.h"
#include "utilities.h"
#include "teensy_comm.h"
#include "config.h"
#include "api_helpers.h"
#include "config.h"
#include <string.h>
#include <ArduinoJson.h>

void handlePutPresetCrossover(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    String freqStr = request->pathArg(1);
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
    const int spl = 0; // Hardcoded SPL

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    Preset* preset = &current_config.presets[presetIndex];
    PEQSet* sets = preset->preference_curve;

    int set_index = -1;
    for (int i = 0; i < MAX_PEQ_SETS; i++) {
        if (sets[i].spl == spl) {
            set_index = i;
            break;
        }
    }

    // If spl=0 set doesn't exist, create it.
    if (set_index == -1) {
        for (int i = 0; i < MAX_PEQ_SETS; i++) {
            if (sets[i].spl == -1) { // find empty slot
                set_index = i;
                sets[i].spl = spl;
                sets[i].num_points = 0;
                break;
            }
        }
    }

    if (set_index == -1) {
        request->send(507, "text/plain", "No available EQ set slots to create default spl=0 set.");
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
    doc["eqType"] = "pref"; // Hardcoded
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
    String state = request->pathArg(1);
    
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
    PEQSet* sets = preset->preference_curve;

    // Check if a PEQ set with spl=0 exists.
    bool spl0_exists = false;
    for (int i = 0; i < MAX_PEQ_SETS; i++) {
        if (sets[i].spl == 0) {
            spl0_exists = true;
            break;
        }
    }

    if (!spl0_exists) {
        // Find an empty slot to create the spl=0 set.
        int empty_slot = -1;
        for (int i = 0; i < MAX_PEQ_SETS; i++) {
            if (sets[i].spl == -1) {
                empty_slot = i;
                break;
            }
        }

        if (empty_slot != -1) {
            sets[empty_slot].spl = 0;
            sets[empty_slot].num_points = 0;
        } else {
            request->send(507, "text/plain", "No available EQ set slots to create default spl=0 set.");
            return;
        }
    }

    bool enabled = (state == "on");
    
    preset->EQEnabled = enabled;
    
    scheduleConfigWrite();
    
    // Prepare and send response
    DynamicJsonDocument doc(128);
    doc["status"] = "ok";
    doc["enabled"] = enabled;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
    
    // Send command to Teensy
    sendOnOffToTeensy(CMD_SET_EQ_ENABLED, enabled);
    
    // Broadcast update
    broadcastWebSocket(response);
}

void handleResetEQFilters(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    
    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }
    
    Preset* preset = &current_config.presets[presetIndex];
    
    // Reset preference curve
    for (int i = 0; i < MAX_PEQ_SETS; i++) {
        preset->preference_curve[i].spl = -1;
        preset->preference_curve[i].num_points = 0;
    }
    // Add default set at 0dB SPL
    preset->preference_curve[0].spl = 0;
    preset->preference_curve[0].num_points = 0;
    
    scheduleConfigWrite();
    
    // Send command to Teensy to reset EQ filters
    sendToTeensy(CMD_RESET_EQ_FILTERS, "pref");
    
    // Prepare and send response
    DynamicJsonDocument doc(128);
    doc["status"] = "ok";
    doc["eqType"] = "pref";
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
    
    // Broadcast update
    broadcastWebSocket(response);
}

