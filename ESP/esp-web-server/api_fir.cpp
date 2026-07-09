#include <Arduino.h>         // Core Arduino functionality
#include <ArduinoJson.h>     // JSON parsing and generation
#include <LittleFS.h>        // File system access
#include <Wire.h>            // I2C communication
#include <string.h>          // For strtok, strlen, strncmp
#include "globals.h"
#include "config.h"
#include "web_server.h"
#include "websocket.h"
#include "teensy_comm.h"
#include "utilities.h"
#include "api_helpers.h"

using namespace ArduinoJson;

void handleGetFirFiles(AsyncWebServerRequest *request) {
    // The file list is served from a cache that is refreshed asynchronously
    // over the Teensy link (at boot, when the Teensy reboots, and after each
    // request so the next fetch is fresh).
    requestFirFilesRefresh();

    // strtok modifies its input, so work on a copy of the cache
    static char listCopy[768];
    strlcpy(listCopy, getCachedFirFiles(), sizeof(listCopy));

    if (strlen(listCopy) == 0) {
        request->send(200, "application/json", "[]");
        return;
    }

    // The cache is a newline-separated list of filenames
    // We need to convert this into a JSON array
    DynamicJsonDocument doc(1024);
    JsonArray files = doc.to<JsonArray>();

    // Parse the newline-separated list
    char* line = strtok(listCopy, "\n");
    while (line != NULL) {
        // Skip empty lines and error messages
        if (strlen(line) > 0 && strncmp(line, "ERROR", 5) != 0) {
            // Add the filename to the array
            files.add(line);
        }
        line = strtok(NULL, "\n");
    }

    // Serialize the response
    String jsonResponse;
    serializeJson(files, jsonResponse);

    // Send the response
    request->send(200, "application/json", jsonResponse);
}

void handlePutPresetFir(AsyncWebServerRequest *request) {
    if (!request->hasParam("preset_name") || !request->hasParam("speaker") || !request->hasParam("file")) {
        request->send(400, "text/plain", "Missing required parameters");
        return;
    }
    String presetName = request->getParam("preset_name")->value();
    String speaker = request->getParam("speaker")->value();
    String filename = request->getParam("file")->value();
    
    if (speaker != "left" && speaker != "right" && speaker != "sub") {
        request->send(400, "text/plain", "Invalid speaker");
        return;
    }

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    // Update the FIR filter filename for the specified speaker
    Preset* preset = &current_config.presets[presetIndex];
    if (speaker == "left") {
        preset->FIRFilters.left = filename;
    } else if (speaker == "right") {
        preset->FIRFilters.right = filename;
    } else if (speaker == "sub") {
        preset->FIRFilters.sub = filename;
    }

    scheduleConfigWrite();

    // Push the new filename to the Teensy and reload if this preset is active
    if (presetIndex == current_config.active_preset_index) {
        sendToTeensy(CMD_SET_FIR, speaker, filename);
        loadFirFilters();
    }

    // Prepare and send response
    StaticJsonDocument<192> doc;
    doc["messageType"] = "firChanged";
    doc["presetName"] = presetName;
    doc["status"] = "ok";
    doc["speaker"] = speaker;
    doc["filename"] = filename;
    sendJsonAndBroadcast(request, doc);
}

void handlePutPresetFirEnabled(AsyncWebServerRequest *request) {
    if (!request->hasParam("preset_name")) {
        request->send(400, "text/plain", "Missing preset_name parameter");
        return;
    }
    if (!request->hasParam("state")) {
        request->send(400, "text/plain", "Missing state parameter");
        return;
    }
    String presetName = request->getParam("preset_name")->value();
    String state = request->getParam("state")->value();
    
    if (state != "on" && state != "off") {
        request->send(400, "text/plain", "Invalid state");
        return;
    }

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    // Update the FIR filter enabled state
    bool enabled = (state == "on");
    current_config.presets[presetIndex].FIRFiltersEnabled = enabled;
    scheduleConfigWrite();

    if (presetIndex == current_config.active_preset_index) {
        sendOnOffToTeensy(CMD_SET_FIR_ENABLED, enabled);
        if (enabled) {
            loadFirFilters();
        }
    }

    // Prepare and send response
    StaticJsonDocument<192> doc;
    doc["messageType"] = "firEnabledChanged";
    doc["presetName"] = presetName;
    doc["status"] = "ok";
    doc["FIRFiltersEnabled"] = enabled;
    sendJsonAndBroadcast(request, doc);
}