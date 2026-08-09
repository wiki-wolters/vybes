#include "globals.h"
#include "web_server.h"
#include "websocket.h"
#include "utilities.h"
#include "teensy_comm.h"
#include "compare_mode.h"
#include "config.h"
#include "api_helpers.h"
#include <string.h>
#include <ArduinoJson.h>

// Find the input-EQ set for spl=0, creating it in a free slot if needed.
// Returns nullptr when all slots are taken by other SPL values.
static PEQSet* getOrCreateSpl0Set(Preset* preset) {
    PEQSet* sets = preset->inputEq.sets;
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

// 409 for a write to a locked crossover point without confirm=true. This one
// is JSON (not text/plain): the UI dispatches on the locked flag.
static esp_err_t replyLocked(PsychicRequest* request, const char* id) {
    JsonDocument doc;
    char message[96];
    snprintf(message, sizeof(message),
             "Crossover point %s is locked. Re-send with confirm=true to apply.", id);
    doc["error"] = message;
    doc["locked"] = true;
    String buffer;
    serializeJson(doc, buffer);
    return request->reply(409, "application/json", buffer.c_str());
}

// 409 for an edit that would leave a floor-protected output's high-pass
// below its hpFloor (the driver-protection backstop).
static esp_err_t replyFloorViolation(PsychicRequest* request, const Preset& preset, int outputIndex) {
    const Output& output = preset.outputs[outputIndex];
    char message[128];
    snprintf(message, sizeof(message), "Output %d (%s) requires a high-pass at or above %u Hz",
             outputIndex + 1, output.label, output.hpFloor);
    return request->reply(409, "text/plain", message);
}

// Re-send the resolved HP/LP frequencies of every active-preset output that
// references the given crossover point.
static void syncCrossoverReferencesToTeensy(int presetIndex, const Preset& preset, const char* id) {
    if (presetIndex != current_config.active_preset_index) {
        return;
    }
    for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
        const Output& output = preset.outputs[ch];
        if (strcmp(output.hp.xover, id) == 0 || strcmp(output.lp.xover, id) == 0) {
            sendOutputFiltersToTeensy(ch, preset);
        }
    }
}

// PUT /preset/crossover?preset_name=&id=&frequency=&confirm=
// Points are shared: every output filter referencing the id follows.
// Safety semantics:
//  - locked points reject writes without confirm=true (409, locked flag)
//  - a change may never leave an output's HP below its hpFloor (409)
esp_err_t handlePutPresetCrossover(PsychicRequest *request) {
    if (!request->hasParam("preset_name") || !request->hasParam("frequency") || !request->hasParam("id")) {
        return request->reply(400, "text/plain", "Missing required parameters");
    }
    String presetName = request->getParam("preset_name")->value();
    String id = request->getParam("id")->value();
    int freq = request->getParam("frequency")->value().toInt();
    bool confirmed = request->hasParam("confirm") && request->getParam("confirm")->value() == "true";

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        return request->reply(404, "text/plain", "Preset not found");
    }
    Preset* preset = &current_config.presets[presetIndex];

    int xoverIndex = find_crossover_by_id(*preset, id.c_str());
    if (xoverIndex == -1) {
        return request->reply(404, "text/plain", "Crossover point not found");
    }
    CrossoverPoint& point = preset->crossovers[xoverIndex];

    if (freq < point.min || freq > point.max) {
        char message[80];
        snprintf(message, sizeof(message), "Crossover frequency must be between %u and %u Hz",
                 point.min, point.max);
        return request->reply(400, "text/plain", message);
    }

    if (point.locked && !confirmed) {
        return replyLocked(request, point.id);
    }

    {
        ConfigLock lock;
        uint16_t previousFreq = point.freq;
        point.freq = freq;
        int violation = hp_floor_violation(*preset);
        if (violation >= 0) {
            point.freq = previousFreq;
            return replyFloorViolation(request, *preset, violation);
        }
        scheduleConfigWrite();
    }

    syncCrossoverReferencesToTeensy(presetIndex, *preset, point.id);

    JsonDocument doc;
    doc["messageType"] = "crossoverChanged";
    doc["presetName"] = presetName;
    doc["status"] = "ok";
    doc["id"] = id;
    doc["crossoverFreq"] = freq;
    return sendJsonAndBroadcast(request, doc);
}

// PUT /preset/crossover/enabled?preset_name=&id=&enabled=&confirm=
// Bypass/enable a crossover point: toggles mode between 'xover' and 'off' on
// every filter that references it (the xover ref is kept so re-enabling
// restores it). Locked points need confirm=true; hpFloor blocks bypassing a
// protective HP entirely, confirmed or not.
esp_err_t handlePutPresetCrossoverEnabled(PsychicRequest *request) {
    if (!request->hasParam("preset_name") || !request->hasParam("id")) {
        return request->reply(400, "text/plain", "Missing required parameters");
    }
    String presetName = request->getParam("preset_name")->value();
    String id = request->getParam("id")->value();
    String state = request->hasParam("enabled") ? request->getParam("enabled")->value() : "";
    bool confirmed = request->hasParam("confirm") && request->getParam("confirm")->value() == "true";

    if (state != "on" && state != "off") {
        return request->reply(400, "text/plain", "Invalid state");
    }
    bool enabled = (state == "on");

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        return request->reply(404, "text/plain", "Preset not found");
    }
    Preset* preset = &current_config.presets[presetIndex];

    int xoverIndex = find_crossover_by_id(*preset, id.c_str());
    if (xoverIndex == -1) {
        return request->reply(404, "text/plain", "Crossover point not found");
    }

    if (preset->crossovers[xoverIndex].locked && !confirmed) {
        return replyLocked(request, preset->crossovers[xoverIndex].id);
    }

    {
        ConfigLock lock;
        // Flip every referencing filter, remembering the previous modes so a
        // floor violation can undo the whole toggle
        FilterMode previousModes[NUM_OUTPUTS][2];
        for (int i = 0; i < NUM_OUTPUTS; i++) {
            FilterSection* sections[2] = {&preset->outputs[i].hp, &preset->outputs[i].lp};
            for (int s = 0; s < 2; s++) {
                previousModes[i][s] = sections[s]->mode;
                if (strcmp(sections[s]->xover, id.c_str()) == 0 &&
                    sections[s]->mode != FilterMode::Manual) {
                    sections[s]->mode = enabled ? FilterMode::Xover : FilterMode::Off;
                }
            }
        }
        int violation = hp_floor_violation(*preset);
        if (violation >= 0) {
            for (int i = 0; i < NUM_OUTPUTS; i++) {
                preset->outputs[i].hp.mode = previousModes[i][0];
                preset->outputs[i].lp.mode = previousModes[i][1];
            }
            return replyFloorViolation(request, *preset, violation);
        }
        scheduleConfigWrite();
    }

    syncCrossoverReferencesToTeensy(presetIndex, *preset, id.c_str());

    JsonDocument doc;
    doc["messageType"] = "crossoverEnabledChanged";
    doc["presetName"] = presetName;
    doc["status"] = "ok";
    doc["id"] = id;
    doc["crossoverEnabled"] = enabled;
    return sendJsonAndBroadcast(request, doc);
}

// PUT /preset/dynamics?preset_name= - replace the preset's whole dynamics
// (multiband compressor) block. The UI always sends the full object, so
// there is no per-field endpoint.
esp_err_t handlePutPresetDynamics(PsychicRequest *request, JsonVariant &json) {
    if (!request->hasParam("preset_name")) {
        return request->reply(400, "text/plain", "Missing required parameters");
    }
    String presetName = request->getParam("preset_name")->value();

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        return request->reply(404, "text/plain", "Preset not found");
    }

    JsonObject body = json.as<JsonObject>();
    if (body.isNull()) {
        return request->reply(400, "text/plain", "Expected a JSON dynamics object");
    }

    Preset* preset = &current_config.presets[presetIndex];
    {
        ConfigLock lock;
        Dynamics& dyn = preset->dynamics;
        dyn.enabled = body["enabled"] | dyn.enabled;
        strlcpy(dyn.mode, body["mode"] | dyn.mode, sizeof(dyn.mode));
        dyn.strength = clampf(body["strength"] | dyn.strength, 0.0f, 100.0f);
        dyn.xoverLow = clampf(body["xoverLow"] | dyn.xoverLow, 40.0f, 1000.0f);
        dyn.xoverHigh = clampf(body["xoverHigh"] | dyn.xoverHigh, 2.0f * dyn.xoverLow, 12000.0f);
        dyn.voicePriority = clampf(body["voicePriority"] | dyn.voicePriority, 0.0f, 24.0f);
        JsonArray bands = body["bands"];
        int count = 0;
        for (JsonObject band : bands) {
            if (count >= COMP_BANDS) break;
            CompBand& target = dyn.bands[count++];
            target.threshold = clampf(band["threshold"] | target.threshold, -60.0f, 0.0f);
            target.ratio = clampf(band["ratio"] | target.ratio, 1.0f, 20.0f);
            target.attack = clampf(band["attack"] | target.attack, 0.5f, 500.0f);
            target.release = clampf(band["release"] | target.release, 10.0f, 2000.0f);
            target.makeup = clampf(band["makeup"] | target.makeup, -12.0f, 12.0f);
            target.bypass = band["bypass"] | target.bypass;
        }
        scheduleConfigWrite();
    }

    if (presetIndex == current_config.active_preset_index) {
        sendDynamicsToTeensy(preset->dynamics);
    }

    JsonDocument responseDoc;
    responseDoc["messageType"] = "dynamicsChanged";
    responseDoc["presetName"] = presetName;
    responseDoc["status"] = "ok";
    dynamics_to_json(preset->dynamics, responseDoc.createNestedObject("dynamics"));
    return sendJsonAndBroadcast(request, responseDoc);
}

// POST /comp/solo?band= - audition one compressor band (-1 restores all).
// Transient: relayed to the Teensy, never stored in the preset.
esp_err_t handlePostCompSolo(PsychicRequest *request) {
    if (!request->hasParam("band")) {
        return request->reply(400, "text/plain", "Missing required parameters");
    }
    int band = request->getParam("band")->value().toInt();
    if (band < -1 || band >= COMP_BANDS) {
        return request->reply(400, "text/plain", "Band out of range");
    }
    sendIntToTeensy(CMD_SET_COMP_SOLO, band);
    return request->reply(204);
}

esp_err_t handlePutPresetEQPoints(PsychicRequest *request, JsonVariant &json) {
    if (!request->hasParam("preset_name")) {
        return request->reply(400, "text/plain", "Missing required parameters");
    }
    String presetName = request->getParam("preset_name")->value();

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        return request->reply(404, "text/plain", "Preset not found");
    }

    Preset* preset = &current_config.presets[presetIndex];
    PEQSet* target_set = getOrCreateSpl0Set(preset);
    if (target_set == nullptr) {
        return request->reply(507, "text/plain", "No available EQ set slots to create default spl=0 set.");
    }

    JsonArray pointsArray = json.as<JsonArray>();
    if (pointsArray.isNull()) {
        return request->reply(400, "text/plain", "Expected a JSON array of PEQ points");
    }
    if ((int)pointsArray.size() > MAX_PEQ_POINTS) {
        return request->reply(400, "text/plain", "Too many PEQ points");
    }

    // Points are stored sequentially: array order defines the band index.
    int prev_num_points = target_set->num_points;
    int count = 0;
    bool changed[MAX_PEQ_POINTS];

    {
        ConfigLock lock;
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
    }

    if (presetIndex == current_config.active_preset_index) {
        // Queue only the points that actually changed...
        for (int i = 0; i < count; i++) {
            if (changed[i]) {
                sendInputEqPointToTeensy(i, target_set->points[i]);
            }
        }
        // ...and disable every band beyond the active points with a single command
        char fromIndex[8];
        snprintf(fromIndex, sizeof(fromIndex), "%d", count);
        sendToTeensy(CMD_RESET_INPUT_EQ, fromIndex);
        compareOnStateChanged();
    }

    JsonDocument responseDoc;
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

    return request->reply(204);
}

esp_err_t handlePutPresetEQPoint(PsychicRequest *request, JsonVariant &json) {
    if (!request->hasParam("preset_name")) {
        return request->reply(400, "text/plain", "Missing required parameters");
    }
    String presetName = request->getParam("preset_name")->value();

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        return request->reply(404, "text/plain", "Preset not found");
    }

    JsonObject point = json.as<JsonObject>();
    if (point.isNull()) {
        return request->reply(400, "text/plain", "Expected a JSON PEQ point object");
    }

    int id = point["id"] | -1;
    if (id < 0 || id >= MAX_PEQ_POINTS) {
        return request->reply(400, "text/plain", "PEQ point ID out of bounds");
    }

    Preset* preset = &current_config.presets[presetIndex];
    PEQSet* target_set = getOrCreateSpl0Set(preset);
    if (target_set == nullptr) {
        return request->reply(507, "text/plain", "No available EQ set slots to create default spl=0 set.");
    }

    // Allow updating an existing point or appending directly after the last
    // one; a larger id would mark the skipped-over stale points as active.
    if (id > target_set->num_points) {
        return request->reply(400, "text/plain", "PEQ point ID would leave a gap");
    }

    {
        ConfigLock lock;
        PEQPoint& stored = target_set->points[id];
        stored.freq = clampf(point["freq"] | 1000.0f, 20.0f, 20000.0f);
        stored.gain = clampf(point["gain"] | 0.0f, -15.0f, 15.0f);
        stored.q    = clampf(point["q"] | 1.0f, 0.1f, 10.0f);
        if (id >= target_set->num_points) {
            target_set->num_points = id + 1;
        }
        scheduleConfigWrite();
    }

    if (presetIndex == current_config.active_preset_index) {
        sendInputEqPointToTeensy(id, target_set->points[id]);
        compareOnStateChanged();
    }

    return request->reply(204);
}

esp_err_t handlePutPresetEQEnabled(PsychicRequest *request) {
    if (!request->hasParam("preset_name") || !request->hasParam("enabled")) {
        return request->reply(400, "text/plain", "Missing required parameters");
    }
    String presetName = request->getParam("preset_name")->value();
    String state = request->getParam("enabled")->value();

    if (state != "on" && state != "off") {
        return request->reply(400, "text/plain", "Invalid state. Must be 'on' or 'off'");
    }

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        return request->reply(404, "text/plain", "Preset not found");
    }

    Preset* preset = &current_config.presets[presetIndex];
    if (getOrCreateSpl0Set(preset) == nullptr) {
        return request->reply(507, "text/plain", "No available EQ set slots to create default spl=0 set.");
    }

    bool enabled = (state == "on");
    {
        ConfigLock lock;
        preset->inputEq.enabled = enabled;
        scheduleConfigWrite();
    }

    if (presetIndex == current_config.active_preset_index) {
        sendOnOffToTeensy(CMD_SET_INPUT_EQ_ENABLED, enabled);
        compareOnStateChanged();
    }

    JsonDocument doc;
    doc["messageType"] = "eqEnabledChanged";
    doc["presetName"] = presetName;
    doc["status"] = "ok";
    doc["enabled"] = enabled;
    return sendJsonAndBroadcast(request, doc);
}
