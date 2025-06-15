#include "globals.h"
#include "web_server.h"
#include "websocket.h"
#include "utilities.h"
#include "teensy_comm.h"
#include "config.h"
#include "api_helpers.h"
#include <string.h>

void handlePutPresetDelay(AsyncWebServerRequest *request) {
    String speaker = request->pathArg(0);
    String delayStr = request->pathArg(1);
    float delay = delayStr.toFloat();

    int presetIndex = current_config.active_preset_index;
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Active preset not found");
        return;
    }

    Preset* preset = &current_config.presets[presetIndex];

    if (speaker == "left") {
        preset->delay.left = delay;
    } else if (speaker == "right") {
        preset->delay.right = delay;
    } else if (speaker == "sub") {
        preset->delay.sub = delay;
    } else {
        request->send(400, "text/plain", "Invalid speaker");
        return;
    }

    scheduleConfigWrite();
    sendToTeensy("delay", speaker + "," + delayStr);
    request->send(200, "application/json", "{}");
}

void handlePutPresetDelayNamed(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    String speaker = request->pathArg(1);
    String delayStr = request->pathArg(2);
    float delay = delayStr.toFloat();

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    Preset* preset = &current_config.presets[presetIndex];

    if (speaker == "left") {
        preset->delay.left = delay;
    } else if (speaker == "right") {
        preset->delay.right = delay;
    } else if (speaker == "sub") {
        preset->delay.sub = delay;
    } else {
        request->send(400, "text/plain", "Invalid speaker");
        return;
    }

    scheduleConfigWrite();
    sendToTeensy("delay_" + speaker, delayStr);
    request->send(200, "application/json", "{}");
}

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
    String type = request->pathArg(1);
    String freqStr = request->pathArg(2);
    int freq = freqStr.toInt();

    if (type != "lowPass" && type != "highPass") {
        request->send(400, "text/plain", "Invalid crossover type");
        return;
    }

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    Preset* preset = &current_config.presets[presetIndex];

    if (type == "lowPass") {
        preset->crossover.lowPass = freq;
    } else { // highPass
        preset->crossover.highPass = freq;
    }

    scheduleConfigWrite();
    sendToTeensy("crossover_" + type, freqStr);
    request->send(200, "application/json", "{}");
}

void handlePutPresetEqualLoudness(AsyncWebServerRequest *request) {
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

    current_config.presets[presetIndex].equalLoudness = enabled;

    scheduleConfigWrite();
    sendToTeensy("equalLoudness", state);
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
    
    // TODO: Send updated PEQ points to Teensy if needed
}

