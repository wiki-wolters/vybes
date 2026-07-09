#include "globals.h"
#include "web_server.h"
#include "websocket.h"
#include "utilities.h"
#include "teensy_comm.h"
#include "config.h"
#include "api_helpers.h"
#include <string.h>
#include <ArduinoJson.h>

// Find the preference-curve set for spl=0, creating it in a free slot if
// needed. Returns nullptr when all slots are taken by other SPL values.
static PEQSet* getOrCreateSpl0Set(Preset* preset) {
    PEQSet* sets = preset->preference_curve;
    for (int i = 0; i < MAX_PEQ_SETS; i++) {
        if (sets[i].spl == 0) {
            return &sets[i];
        }
    }
    for (int i = 0; i < MAX_PEQ_SETS; i++) {
        if (sets[i].spl == -1) { // free slot
            sets[i].spl = 0;
            sets[i].num_points = 0;
            return &sets[i];
        }
    }
    return nullptr;
}

static float clampf(float value, float lo, float hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

void handlePutPresetCrossover(AsyncWebServerRequest *request) {
    if (!request->hasParam("preset_name") || !request->hasParam("frequency")) {
        request->send(400, "text/plain", "Missing required parameters");
        return;
    }
    String presetName = request->getParam("preset_name")->value();
    String freqStr = request->getParam("frequency")->value();
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

    // Only push to the Teensy when the edited preset is the active one
    if (presetIndex == current_config.active_preset_index) {
        sendFloatToTeensy(CMD_SET_CROSSOVER_FREQ, freq);
    }

    StaticJsonDocument<192> doc;
    doc["messageType"] = "crossoverChanged";
    doc["presetName"] = presetName;
    doc["status"] = "ok";
    doc["crossoverFreq"] = freq;
    sendJsonAndBroadcast(request, doc);
}

void handlePutPresetCrossoverEnabled(AsyncWebServerRequest *request) {
    if (!request->hasParam("preset_name") || !request->hasParam("enabled")) {
        request->send(400, "text/plain", "Missing required parameters");
        return;
    }
    String presetName = request->getParam("preset_name")->value();
    String state = request->getParam("enabled")->value();

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

    if (presetIndex == current_config.active_preset_index) {
        sendOnOffToTeensy(CMD_SET_CROSSOVER_ENABLED, enabled);
    }

    StaticJsonDocument<192> doc;
    doc["messageType"] = "crossoverEnabledChanged";
    doc["presetName"] = presetName;
    doc["status"] = "ok";
    doc["crossoverEnabled"] = enabled;
    sendJsonAndBroadcast(request, doc);
}

void handlePutPresetEQPoints(AsyncWebServerRequest *request, JsonVariant &json) {
    if (!request->hasParam("preset_name")) {
        request->send(400, "text/plain", "Missing required parameters");
        return;
    }
    String presetName = request->getParam("preset_name")->value();

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    Preset* preset = &current_config.presets[presetIndex];
    PEQSet* target_set = getOrCreateSpl0Set(preset);
    if (target_set == nullptr) {
        request->send(507, "text/plain", "No available EQ set slots to create default spl=0 set.");
        return;
    }

    JsonArray pointsArray = json.as<JsonArray>();
    if (pointsArray.isNull()) {
        request->send(400, "text/plain", "Expected a JSON array of PEQ points");
        return;
    }
    if ((int)pointsArray.size() > MAX_PEQ_POINTS) {
        request->send(400, "text/plain", "Too many PEQ points");
        return;
    }

    // Points are stored sequentially: array order defines the band index.
    int prev_num_points = target_set->num_points;
    int count = 0;
    bool changed[MAX_PEQ_POINTS];

    for (JsonObject point : pointsArray) {
        float new_freq = clampf(point["freq"] | 1000.0f, 20.0f, 20000.0f);
        float new_gain = clampf(point["gain"] | 0.0f, -15.0f, 15.0f);
        float new_q    = clampf(point["q"] | 1.0f, 0.1f, 10.0f);

        PEQPoint& stored = target_set->points[count];
        changed[count] = (count >= prev_num_points) ||
                         (new_freq != stored.freq) ||
                         (new_gain != stored.gain) ||
                         (new_q != stored.q);
        stored.freq = new_freq;
        stored.gain = new_gain;
        stored.q = new_q;
        count++;
    }
    target_set->num_points = count;
    scheduleConfigWrite();

    if (presetIndex == current_config.active_preset_index) {
        // Queue only the points that actually changed...
        for (int i = 0; i < count; i++) {
            if (changed[i]) {
                sendEqPointToTeensy(i, target_set->points[i]);
            }
        }
        // ...and disable every band beyond the active points with a single command
        char fromIndex[8];
        snprintf(fromIndex, sizeof(fromIndex), "%d", count);
        sendToTeensy(CMD_RESET_EQ_FILTERS, fromIndex);
    }

    request->send(204);

    StaticJsonDocument<192> responseDoc;
    responseDoc["messageType"] = "eqPointsChanged";
    responseDoc["presetName"] = presetName;
    responseDoc["status"] = "ok";
    responseDoc["eqType"] = "pref";
    responseDoc["spl"] = 0;
    responseDoc["numPoints"] = target_set->num_points;
    char buffer[192];
    size_t len = serializeJson(responseDoc, buffer, sizeof(buffer));
    if (len > 0 && len < sizeof(buffer)) {
        broadcastWebSocket(buffer);
    }
}

void handlePutPresetEQPoint(AsyncWebServerRequest *request, JsonVariant &json) {
    if (!request->hasParam("preset_name")) {
        request->send(400, "text/plain", "Missing required parameters");
        return;
    }
    String presetName = request->getParam("preset_name")->value();

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    JsonObject point = json.as<JsonObject>();
    if (point.isNull()) {
        request->send(400, "text/plain", "Expected a JSON PEQ point object");
        return;
    }

    int id = point["id"] | -1;
    if (id < 0 || id >= MAX_PEQ_POINTS) {
        request->send(400, "text/plain", "PEQ point ID out of bounds");
        return;
    }

    Preset* preset = &current_config.presets[presetIndex];
    PEQSet* target_set = getOrCreateSpl0Set(preset);
    if (target_set == nullptr) {
        request->send(507, "text/plain", "No available EQ set slots to create default spl=0 set.");
        return;
    }

    PEQPoint& stored = target_set->points[id];
    stored.freq = clampf(point["freq"] | 1000.0f, 20.0f, 20000.0f);
    stored.gain = clampf(point["gain"] | 0.0f, -15.0f, 15.0f);
    stored.q    = clampf(point["q"] | 1.0f, 0.1f, 10.0f);
    if (id >= target_set->num_points) {
        target_set->num_points = id + 1;
    }
    scheduleConfigWrite();

    if (presetIndex == current_config.active_preset_index) {
        sendEqPointToTeensy(id, stored);
    }

    request->send(204);
}

void handlePutPresetEQEnabled(AsyncWebServerRequest *request) {
    if (!request->hasParam("preset_name") || !request->hasParam("enabled")) {
        request->send(400, "text/plain", "Missing required parameters");
        return;
    }
    String presetName = request->getParam("preset_name")->value();
    String state = request->getParam("enabled")->value();

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
    if (getOrCreateSpl0Set(preset) == nullptr) {
        request->send(507, "text/plain", "No available EQ set slots to create default spl=0 set.");
        return;
    }

    bool enabled = (state == "on");
    preset->EQEnabled = enabled;
    scheduleConfigWrite();

    if (presetIndex == current_config.active_preset_index) {
        sendOnOffToTeensy(CMD_SET_EQ_ENABLED, enabled);
    }

    StaticJsonDocument<192> doc;
    doc["messageType"] = "eqEnabledChanged";
    doc["presetName"] = presetName;
    doc["status"] = "ok";
    doc["enabled"] = enabled;
    sendJsonAndBroadcast(request, doc);
}
