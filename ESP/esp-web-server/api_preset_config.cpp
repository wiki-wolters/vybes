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
    unsigned int freq = freqStr.toInt();

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
    String state = request->pathArg(2);
    
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

void handlePutPresetEQPoints(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
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

    Serial.printf("Updating EQ points for preset '%s' at SPL %d with body %s\n", presetName.c_str(), spl, (const char*)data);
    DynamicJsonDocument doc(1024); // Adjust size as needed
    DeserializationError error = deserializeJson(doc, (const char*)data);

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

    for (JsonObject point : pointsArray) {
        int id = point["id"].as<int>();
        if (id >= MAX_PEQ_POINTS) {
            Serial.printf("Error: PEQ point ID %d is out of bounds. Max is %d.\n", id, MAX_PEQ_POINTS - 1);
            continue; 
        }

        float new_freq = point["freq"].as<float>();
        float new_gain = point["gain"].as<float>();
        float new_q = point["q"].as<float>();

        if (new_freq != target_set->points[id].freq ||
            new_gain != target_set->points[id].gain ||
            new_q != target_set->points[id].q
        ) {
            target_set->points[id].freq = new_freq;
            target_set->points[id].gain = new_gain;
            target_set->points[id].q    = new_q;
            
            String pointData = String(new_freq, 1) + " " +
                             String(new_q, 2) + " " +
                             String(new_gain, 2);
            sendToTeensy(CMD_SET_EQ_FILTER, String(id), pointData);
        }
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

    if (enabled) {
        // Re-apply the EQ points
        for (int i = 0; i < MAX_PEQ_SETS; i++) {
            if (sets[i].spl != -1) {
                for (int j = 0; j < sets[i].num_points; j++) {
                    String pointData = String(sets[i].points[j].freq, 1) + " " +
                                     String(sets[i].points[j].q, 2) + " " +
                                     String(sets[i].points[j].gain, 2);
                    sendToTeensy(CMD_SET_EQ_FILTER, String(j), pointData);
                }
            }
        }
    }
    
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

