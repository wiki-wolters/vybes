#include "globals.h"
#include "web_server.h"
#include "websocket.h"
#include "utilities.h"
#include "config.h"
#include "api_helpers.h"
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

    String response;
    serializeJson(doc, response);
    return request->reply(200, "application/json", response.c_str());
}

esp_err_t handlePostPresetCreate(PsychicRequest *request) {
    if (!request->hasParam("name")) {
        return request->reply(400, "text/plain", "Missing required parameters");
    }
    String presetName = request->getParam("name")->value();

    if (presetName.length() == 0 || presetName.length() >= PRESET_NAME_MAX_LEN) {
        return request->reply(400, "text/plain", "Preset name too long");
    }

    if (find_preset_by_name(presetName.c_str()) != -1) {
        return request->reply(409, "text/plain", "Preset name already exists");
    }

    int newIndex = find_empty_preset_slot();
    if (newIndex == -1) {
        return request->reply(507, "text/plain", "Maximum number of presets reached");
    }

    // Create new preset with default values
    ConfigLock lock;
    current_config.presets[newIndex] = Preset();
    strcpy(current_config.presets[newIndex].name, presetName.c_str());
    
    // Initialize delay and crossover settings
    current_config.presets[newIndex].delayEnabled = false;
    current_config.presets[newIndex].crossoverFreq = 80;
    current_config.presets[newIndex].crossoverEnabled = false;
    current_config.presets[newIndex].EQEnabled = false;
    current_config.presets[newIndex].FIRFiltersEnabled = false;
    
    // Initialize the first PEQ set for both curves as this is what the UI expects
    current_config.presets[newIndex].preference_curve[0].spl = 0;
    current_config.presets[newIndex].preference_curve[0].num_points = 3;

    // Set first 3 default points
    for (int k = 0; k < 3; k++) {
        current_config.presets[newIndex].preference_curve[0].points[k] = PEQPoint();
        current_config.presets[newIndex].preference_curve[0].points[k].freq = 100 * pow(10, k);
    }

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
    strcpy(current_config.presets[destIndex].name, destName.c_str());

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
    strcpy(current_config.presets[presetIndex].name, newName.c_str());
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
        current_config.presets[presetIndex].delayEnabled = enabled;
        scheduleConfigWrite();
    }

    // Send command to Teensy only when editing the active preset
    if (presetIndex == current_config.active_preset_index) {
        sendOnOffToTeensy(CMD_SET_DELAY_ENABLED, enabled);
    }

    // Prepare response
    JsonDocument doc;
    doc["messageType"] = "delayEnabledChanged";
    doc["presetName"] = presetName;
    doc["status"] = "ok";
    doc["enabled"] = enabled;
    return sendJsonAndBroadcast(request, doc);
}

esp_err_t handlePutPresetDelayNamed(PsychicRequest *request) {
    if (!request->hasParam("preset_name") || !request->hasParam("speaker") || !request->hasParam("value")) {
        return request->reply(400, "text/plain", "Missing required parameters");
    }
    String presetName = request->getParam("preset_name")->value();
    String speaker = request->getParam("speaker")->value();
    String delayUsStr = request->getParam("value")->value();
    
    if (speaker != "left" && speaker != "right" && speaker != "sub") {
        return request->reply(400, "text/plain", "Invalid speaker. Must be 'left', 'right', or 'sub'");
    }
    
    float delayUs = delayUsStr.toFloat();
    if (delayUs < 0 || delayUs > 20000.0f) {
        return request->reply(400, "text/plain", "Delay must be between 0 and 20,000 microseconds");
    }
    
    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        return request->reply(404, "text/plain", "Preset not found");
    }
    
    // Update the delay in the config
    {
        ConfigLock lock;
        if (speaker == "left") {
            current_config.presets[presetIndex].delay.left = delayUs;
        } else if (speaker == "right") {
            current_config.presets[presetIndex].delay.right = delayUs;
        } else if (speaker == "sub") {
            current_config.presets[presetIndex].delay.sub = delayUs;
        }
        scheduleConfigWrite();
    }

    // Send command to Teensy only when editing the active preset
    if (presetIndex == current_config.active_preset_index) {
        sendToTeensy(CMD_SET_DELAYS,
            String((int)current_config.presets[presetIndex].delay.left),
            String((int)current_config.presets[presetIndex].delay.right),
            String((int)current_config.presets[presetIndex].delay.sub));
    }

    // Prepare response
    JsonDocument doc;
    doc["messageType"] = "delayChanged";
    doc["presetName"] = presetName;
    doc["status"] = "ok";
    doc["speaker"] = speaker;
    doc["delayUs"] = delayUs;
    return sendJsonAndBroadcast(request, doc);
}

