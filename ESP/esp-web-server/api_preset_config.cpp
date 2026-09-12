#include "globals.h"
#include "web_server.h"
#include "websocket.h"
#include "utilities.h"
#include "teensy_comm.h"
#include "config.h"
#include "api_helpers.h"
#include <string.h>
#include <ArduinoJson.h>

// The reference-anchor input-EQ set, created in a free slot if needed.
// Returns nullptr when all slots are taken by other roles.
static PEQSet* getOrCreateReferenceSet(Preset* preset) {
    return get_or_create_input_eq_set(preset->inputEq, EQ_SET_REFERENCE);
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
    PEQSet* target_set = getOrCreateReferenceSet(preset);
    if (target_set == nullptr) {
        return request->reply(507, "text/plain", "No available EQ set slots to create the reference set.");
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
        // The loud anchor only ever differs in gain, so it follows every
        // reference edit (docs/DYNAMIC_EQ.md)
        mirror_reference_to_loud(preset->inputEq);
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
    PEQSet* target_set = getOrCreateReferenceSet(preset);
    if (target_set == nullptr) {
        return request->reply(507, "text/plain", "No available EQ set slots to create the reference set.");
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
        mirror_reference_to_loud(preset->inputEq);
        scheduleConfigWrite();
    }

    if (presetIndex == current_config.active_preset_index) {
        sendInputEqPointToTeensy(id, target_set->points[id]);
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
    if (getOrCreateReferenceSet(preset) == nullptr) {
        return request->reply(507, "text/plain", "No available EQ set slots to create the reference set.");
    }

    bool enabled = (state == "on");
    {
        ConfigLock lock;
        preset->inputEq.enabled = enabled;
        scheduleConfigWrite();
    }

    if (presetIndex == current_config.active_preset_index) {
        sendOnOffToTeensy(CMD_SET_INPUT_EQ_ENABLED, enabled);
    }

    JsonDocument doc;
    doc["messageType"] = "eqEnabledChanged";
    doc["presetName"] = presetName;
    doc["status"] = "ok";
    doc["enabled"] = enabled;
    return sendJsonAndBroadcast(request, doc);
}

// --- Dynamic EQ (docs/DYNAMIC_EQ.md) ---

// Broadcast the loud anchor's state. Both the gain write and the delete
// report the same shape so a UI can merge either without refetching.
static void broadcastLoudChanged(const char* presetName, const InputEq& eq) {
    JsonDocument doc;
    doc["messageType"] = "eqLoudChanged";
    doc["presetName"] = presetName;
    doc["status"] = "ok";
    doc["loudVolume"] = eq.loudVolume;
    JsonArray gains = doc.createNestedArray("gains");
    const PEQSet* loud = find_input_eq_set(eq, EQ_SET_LOUD);
    if (loud != nullptr) {
        for (int i = 0; i < loud->num_points; i++) {
            gains.add(loud->points[i].gain);
        }
    }
    // Roomy enough for a max-length preset name plus 15 gains
    char buffer[320];
    size_t len = serializeJson(doc, buffer, sizeof(buffer));
    if (len > 0 && len < sizeof(buffer)) {
        broadcastWebSocket(buffer);
    }
}

// Queue one loud-anchor gain for the Teensy
static void sendLoudGainToTeensy(int index, float gain) {
    char idStr[8], gainStr[12];
    snprintf(idStr, sizeof(idStr), "%d", index);
    snprintf(gainStr, sizeof(gainStr), "%.2f", gain);
    sendToTeensy(CMD_SET_INPUT_EQ_LOUD_GAIN, idStr, gainStr);
}

// PUT /preset/eq/anchors?preset_name= with {referenceVolume, loudVolume}
// Volume percents, not dB: the Teensy applies the same 60*log10(pct/100)
// law the slider does. An absent key keeps its stored value; a loud anchor
// at or below the reference has nothing to interpolate across, so it is
// stored as 0 ("no loud anchor") rather than rejected.
esp_err_t handlePutPresetEQAnchors(PsychicRequest *request, JsonVariant &json) {
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
        return request->reply(400, "text/plain", "Expected a JSON anchors object");
    }

    Preset* preset = &current_config.presets[presetIndex];
    int referenceVolume = body["referenceVolume"] | preset->inputEq.referenceVolume;
    int loudVolume = body["loudVolume"] | preset->inputEq.loudVolume;
    if (referenceVolume < 0 || referenceVolume > 100 || loudVolume < 0 || loudVolume > 100) {
        return request->reply(400, "text/plain", "Anchor volumes must be between 0 and 100");
    }
    if (loudVolume <= referenceVolume) {
        loudVolume = 0;
    }

    {
        ConfigLock lock;
        preset->inputEq.referenceVolume = referenceVolume;
        preset->inputEq.loudVolume = loudVolume;
        scheduleConfigWrite();
    }

    if (presetIndex == current_config.active_preset_index) {
        char refStr[8], loudStr[8];
        snprintf(refStr, sizeof(refStr), "%d", referenceVolume);
        snprintf(loudStr, sizeof(loudStr), "%d", loudVolume);
        sendToTeensy(CMD_SET_INPUT_EQ_ANCHORS, refStr, loudStr);
    }

    JsonDocument doc;
    doc["messageType"] = "eqAnchorsChanged";
    doc["presetName"] = presetName;
    doc["status"] = "ok";
    doc["referenceVolume"] = referenceVolume;
    doc["loudVolume"] = loudVolume;
    return sendJsonAndBroadcast(request, doc);
}

// PUT /preset/eq/loud?preset_name= with {gains: [...]}
// Gains only, aligned to the reference bands: the loud anchor shares their
// frequencies and Qs, so it is created by mirroring rather than described.
// A short array leaves the remaining bands flat.
esp_err_t handlePutPresetEQLoud(PsychicRequest *request, JsonVariant &json) {
    if (!request->hasParam("preset_name")) {
        return request->reply(400, "text/plain", "Missing required parameters");
    }
    String presetName = request->getParam("preset_name")->value();

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        return request->reply(404, "text/plain", "Preset not found");
    }

    JsonArray gains = json["gains"].as<JsonArray>();
    if (gains.isNull()) {
        return request->reply(400, "text/plain", "Expected a JSON object with a gains array");
    }

    Preset* preset = &current_config.presets[presetIndex];
    const PEQSet* reference = find_input_eq_set(preset->inputEq, EQ_SET_REFERENCE);
    int num_points = reference != nullptr ? reference->num_points : 0;
    if ((int)gains.size() > num_points) {
        return request->reply(400, "text/plain", "More gains than reference EQ points");
    }

    PEQSet* loud = nullptr;
    {
        ConfigLock lock;
        loud = get_or_create_input_eq_set(preset->inputEq, EQ_SET_LOUD);
        if (loud == nullptr) {
            return request->reply(507, "text/plain", "No available EQ set slots to create the loud set.");
        }
        // Take the bands from the reference anchor first, then overwrite the
        // gains: a freshly created loud set is otherwise empty
        mirror_reference_to_loud(preset->inputEq);
        int index = 0;
        for (JsonVariant gain : gains) {
            loud->points[index++].gain = clampf(gain | 0.0f, -15.0f, 15.0f);
        }
        for (; index < loud->num_points; index++) {
            loud->points[index].gain = 0.0f;
        }
        scheduleConfigWrite();
    }

    if (presetIndex == current_config.active_preset_index) {
        for (int i = 0; i < loud->num_points; i++) {
            sendLoudGainToTeensy(i, loud->points[i].gain);
        }
    }

    broadcastLoudChanged(presetName.c_str(), preset->inputEq);
    return request->reply(204);
}

// DELETE /preset/eq/loud?preset_name= - drop the loud anchor entirely. The
// Teensy ignores loud gains once the anchor is 0, but they are zeroed too so
// a later re-add can't resurrect a curve the user threw away.
esp_err_t handleDeletePresetEQLoud(PsychicRequest *request) {
    if (!request->hasParam("preset_name")) {
        return request->reply(400, "text/plain", "Missing required parameters");
    }
    String presetName = request->getParam("preset_name")->value();

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        return request->reply(404, "text/plain", "Preset not found");
    }

    Preset* preset = &current_config.presets[presetIndex];
    int cleared = 0;
    {
        ConfigLock lock;
        PEQSet* loud = find_input_eq_set(preset->inputEq, EQ_SET_LOUD);
        if (loud != nullptr) {
            cleared = loud->num_points;
            *loud = PEQSet();
        }
        preset->inputEq.loudVolume = 0;
        scheduleConfigWrite();
    }

    if (presetIndex == current_config.active_preset_index) {
        for (int i = 0; i < cleared; i++) {
            sendLoudGainToTeensy(i, 0.0f);
        }
        char refStr[8];
        snprintf(refStr, sizeof(refStr), "%d", preset->inputEq.referenceVolume);
        sendToTeensy(CMD_SET_INPUT_EQ_ANCHORS, refStr, "0");
    }

    JsonDocument doc;
    doc["messageType"] = "eqLoudChanged";
    doc["presetName"] = presetName;
    doc["status"] = "ok";
    doc["loudVolume"] = 0;
    doc.createNestedArray("gains");
    return sendJsonAndBroadcast(request, doc);
}

// PUT /preset/eq/loudness?preset_name=&enabled= - the ISO 226-derived bass
// compensation applied below the reference anchor. It takes no tuning, so
// this is the whole of its API. Accepts 1/0 as well as the on/off the
// sibling toggles use, because the UI sends the flag as a bare boolean.
esp_err_t handlePutPresetEQLoudness(PsychicRequest *request) {
    if (!request->hasParam("preset_name") || !request->hasParam("enabled")) {
        return request->reply(400, "text/plain", "Missing required parameters");
    }
    String presetName = request->getParam("preset_name")->value();
    String state = request->getParam("enabled")->value();

    bool enabled;
    if (state == "on" || state == "1") {
        enabled = true;
    } else if (state == "off" || state == "0") {
        enabled = false;
    } else {
        return request->reply(400, "text/plain", "Invalid state. Must be '1' or '0'");
    }

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        return request->reply(404, "text/plain", "Preset not found");
    }

    Preset* preset = &current_config.presets[presetIndex];
    {
        ConfigLock lock;
        preset->inputEq.loudness = enabled;
        scheduleConfigWrite();
    }

    if (presetIndex == current_config.active_preset_index) {
        sendOnOffToTeensy(CMD_SET_LOUDNESS, enabled);
    }

    JsonDocument doc;
    doc["messageType"] = "eqLoudnessChanged";
    doc["presetName"] = presetName;
    doc["status"] = "ok";
    doc["loudness"] = enabled;
    return sendJsonAndBroadcast(request, doc);
}
