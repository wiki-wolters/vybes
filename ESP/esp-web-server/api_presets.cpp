#include "globals.h"
#include "web_server.h"
#include "websocket.h"
#include "utilities.h"
#include "config.h"
#include "templates.h"
#include "api_helpers.h"
#include "api_fir.h"
#include "teensy_comm.h"
#include <ArduinoJson.h>
#include <string.h>

// --- API Handlers ---

esp_err_t handleGetPresets(PsychicRequest *request) {
    JsonDocument doc;
    JsonArray presets = doc.to<JsonArray>();

    for (int i = 0; i < MAX_PRESETS; i++) {
        if (strlen(current_config.presets[i].name) > 0) {
            JsonObject preset = presets.createNestedObject();
            preset["name"] = current_config.presets[i].name;
            preset["isCurrent"] = (i == current_config.active_preset_index);
        }
    }

    String response;
    serializeJson(doc, response);
    return request->reply(200, "application/json", response.c_str());
}

// GET /templates - the available preset templates
esp_err_t handleGetTemplates(PsychicRequest *request) {
    JsonDocument doc;
    JsonArray templates = doc.to<JsonArray>();
    for (int i = 0; i < template_count(); i++) {
        const TemplateInfo& info = template_info(i);
        JsonObject entry = templates.createNestedObject();
        entry["id"] = info.id;
        entry["label"] = info.label;
        entry["description"] = info.description;
        entry["outputsUsed"] = info.outputsUsed;
    }

    String response;
    serializeJson(doc, response);
    return request->reply(200, "application/json", response.c_str());
}

// GET /preset?name= - the full V1 preset shape (docs/CHANNEL_ARCHITECTURE.md)
esp_err_t handleGetPreset(PsychicRequest *request) {
    if (!request->hasParam("name")) {
        return request->reply(400, "text/plain", "Missing required parameters");
    }
    String presetName = request->getParam("name")->value();
    int presetIndex = find_preset_by_name(presetName.c_str());

    if (presetIndex == -1) {
        return request->reply(404, "text/plain", "Preset not found");
    }

    const Preset& preset = current_config.presets[presetIndex];
    JsonDocument doc;
    doc["name"] = preset.name;
    doc["isCurrent"] = presetIndex == current_config.active_preset_index;
    doc["template"] = preset.templateId;

    JsonArray crossovers = doc.createNestedArray("crossovers");
    for (int i = 0; i < preset.num_crossovers; i++) {
        crossover_to_json(preset.crossovers[i], crossovers.createNestedObject());
    }

    input_eq_to_json(preset.inputEq, doc.createNestedObject("inputEq"));

    JsonArray outputs = doc.createNestedArray("outputs");
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        output_to_json(preset.outputs[i], outputs.createNestedObject());
    }

    doc["delaysEnabled"] = preset.delaysEnabled;
    doc["firEnabled"] = preset.firEnabled;
    dynamics_to_json(preset.dynamics, doc.createNestedObject("dynamics"));

    JsonObject firPool = doc.createNestedObject("firPool");
    firPool["total"] = FIR_TAP_POOL;
    firPool["used"] = firPoolUsed(preset);

    String response;
    serializeJson(doc, response);
    return request->reply(200, "application/json", response.c_str());
}

// POST /preset?action=create&name=&template= - creates a preset from a
// template (default 2.1). Does not change the active preset.
esp_err_t handlePostPresetCreate(PsychicRequest *request) {
    if (!request->hasParam("name")) {
        return request->reply(400, "text/plain", "Missing required parameters");
    }
    String presetName = request->getParam("name")->value();
    String templateId = request->hasParam("template") && request->getParam("template")->value().length() > 0
                            ? request->getParam("template")->value()
                            : DEFAULT_TEMPLATE_ID;

    if (presetName.length() == 0 || presetName.length() >= PRESET_NAME_MAX_LEN) {
        return request->reply(400, "text/plain", "Preset name too long");
    }

    bool knownTemplate = false;
    for (int i = 0; i < template_count(); i++) {
        if (templateId == template_info(i).id) {
            knownTemplate = true;
            break;
        }
    }
    if (!knownTemplate) {
        return request->reply(400, "text/plain", "Unknown template");
    }

    if (find_preset_by_name(presetName.c_str()) != -1) {
        return request->reply(409, "text/plain", "Preset name already exists");
    }

    int newIndex = find_empty_preset_slot();
    if (newIndex == -1) {
        return request->reply(507, "text/plain", "Maximum number of presets reached");
    }

    ConfigLock lock;
    Preset& preset = current_config.presets[newIndex];
    build_preset_from_template(preset, templateId.c_str());
    strlcpy(preset.name, presetName.c_str(), sizeof(preset.name));

    scheduleConfigWrite();
    return request->reply(201, "application/json", "{}");
}

esp_err_t handlePostPresetCopy(PsychicRequest *request) {
    if (!request->hasParam("source") || !request->hasParam("destination")) {
        return request->reply(400, "text/plain", "Missing required parameters");
    }
    String sourceName = request->getParam("source")->value();
    String destName = request->getParam("destination")->value();

    if (destName.length() == 0 || destName.length() >= PRESET_NAME_MAX_LEN) {
        return request->reply(400, "text/plain", "Destination preset name too long");
    }

    if (find_preset_by_name(destName.c_str()) != -1) {
        return request->reply(409, "text/plain", "Destination preset name already exists");
    }

    int sourceIndex = find_preset_by_name(sourceName.c_str());
    if (sourceIndex == -1) {
        return request->reply(404, "text/plain", "Source preset not found");
    }

    int destIndex = find_empty_preset_slot();
    if (destIndex == -1) {
        return request->reply(507, "text/plain", "Maximum number of presets reached");
    }

    // Copy the preset struct
    ConfigLock lock;
    current_config.presets[destIndex] = current_config.presets[sourceIndex];
    // Update the name
    strlcpy(current_config.presets[destIndex].name, destName.c_str(),
            sizeof(current_config.presets[destIndex].name));

    scheduleConfigWrite();
    return request->reply(201, "application/json", "{}");
}

esp_err_t handlePutPresetRename(PsychicRequest *request) {
    if (!request->hasParam("old_name") || !request->hasParam("new_name")) {
        return request->reply(400, "text/plain", "Missing required parameters");
    }
    String oldName = request->getParam("old_name")->value();
    String newName = request->getParam("new_name")->value();

    if (newName.length() == 0 || newName.length() >= PRESET_NAME_MAX_LEN) {
        return request->reply(400, "text/plain", "Invalid new preset name");
    }

    int existing_preset_index = find_preset_by_name(newName.c_str());
    if (existing_preset_index != -1 && existing_preset_index != find_preset_by_name(oldName.c_str())) {
        return request->reply(409, "text/plain", "New preset name already exists");
    }

    int presetIndex = find_preset_by_name(oldName.c_str());
    if (presetIndex == -1) {
        return request->reply(404, "text/plain", "Preset to rename not found");
    }

    // Update name in config
    ConfigLock lock;
    strlcpy(current_config.presets[presetIndex].name, newName.c_str(),
            sizeof(current_config.presets[presetIndex].name));
    scheduleConfigWrite();

    return request->reply(200, "application/json", "{}");
}

esp_err_t handleDeletePreset(PsychicRequest *request) {
    if (!request->hasParam("name")) {
        return request->reply(400, "text/plain", "Missing required parameters");
    }
    String presetName = request->getParam("name")->value();

    int presetIndex = find_preset_by_name(presetName.c_str());

    if (presetIndex == -1) {
        return request->reply(404, "text/plain", "Preset not found");
    }

    // Refuse to delete the last remaining preset (names can be changed, so
    // checking for "Default" wouldn't protect anything)
    int usedPresets = 0;
    for (int i = 0; i < MAX_PRESETS; i++) {
        if (strlen(current_config.presets[i].name) > 0) {
            usedPresets++;
        }
    }
    if (usedPresets <= 1) {
        return request->reply(400, "text/plain", "Cannot delete the last remaining preset");
    }

    ConfigLock lock;
    // "Delete" by clearing the name, making the slot available
    current_config.presets[presetIndex].name[0] = '\0';

    // If the deleted preset was the active one, switch to the first
    // remaining preset (slot 0 may itself have been deleted earlier)
    if (current_config.active_preset_index == presetIndex) {
        for (int i = 0; i < MAX_PRESETS; i++) {
            if (strlen(current_config.presets[i].name) > 0) {
                current_config.active_preset_index = i;
                break;
            }
        }
    }

    updateTeensyWithActivePresetParameters();
    loadFirFilters();

    scheduleConfigWrite();
    return request->reply(200, "application/json", "{}");
}

esp_err_t handlePutActivePreset(PsychicRequest *request) {
    if (!request->hasParam("name")) {
        return request->reply(400, "text/plain", "Missing required parameters");
    }
    String presetName = request->getParam("name")->value();
    int presetIndex = find_preset_by_name(presetName.c_str());

    if (presetIndex == -1) {
        return request->reply(404, "text/plain", "Preset not found");
    }

    {
        ConfigLock lock;
        current_config.active_preset_index = presetIndex;
        scheduleConfigWrite();
    }
    updateTeensyWithActivePresetParameters();
    loadFirFilters();

    // Broadcast first, then reply (reply ends the request)

    // Prepare data for WebSocket broadcast
    JsonDocument doc;
    doc["messageType"] = "activePresetChanged";
    doc["activePresetName"] = current_config.presets[current_config.active_preset_index].name;
    doc["activePresetIndex"] = current_config.active_preset_index;
    char responseBuffer[192];
    size_t len = serializeJson(doc, responseBuffer, sizeof(responseBuffer));
    if (len > 0 && len < sizeof(responseBuffer)) {
        broadcastWebSocket(responseBuffer);
    } else {
        DebugSerial.println("Error serializing JSON for WebSocket broadcast or buffer too small.");
    }
    return request->reply(200, "application/json", "{}");
}

// PUT /preset/delay/enabled - the preset-level master delay toggle
esp_err_t handlePutPresetDelayEnabled(PsychicRequest *request) {
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

    bool enabled = (state == "on");
    {
        ConfigLock lock;
        current_config.presets[presetIndex].delaysEnabled = enabled;
        scheduleConfigWrite();
    }

    // Send command to Teensy only when editing the active preset
    if (presetIndex == current_config.active_preset_index) {
        sendOnOffToTeensy(CMD_SET_DELAYS_ENABLED, enabled);
    }

    // Prepare response
    JsonDocument doc;
    doc["messageType"] = "delayEnabledChanged";
    doc["presetName"] = presetName;
    doc["status"] = "ok";
    doc["enabled"] = enabled;
    return sendJsonAndBroadcast(request, doc);
}
