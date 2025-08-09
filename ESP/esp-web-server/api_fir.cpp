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

using namespace ArduinoJson;

void handleGetFirFiles(AsyncWebServerRequest *request) {
    // Request a list of all FIR filter files from the Teensy
    char* response = requestFromTeensy(CMD_GET_FILES);
    
    if (response == NULL) {
        request->send(500, "application/json", "{\"error\":\"Failed to communicate with Teensy\"}");
        return;
    }
    
    if (strlen(response) == 0) {
        // If no response, return empty array
        request->send(200, "application/json", "[]");
        return;
    }
    
    // The response from Teensy is a newline-separated list of filenames
    // We need to convert this into a JSON array
    DynamicJsonDocument doc(1024);
    JsonArray files = doc.to<JsonArray>();
    
    // Parse the newline-separated list
    char* line = strtok((char*)response, "\n");
    while (line != NULL) {
        // Skip empty lines and error messages
        if (strlen(line) > 0 && strncmp(line, "ERROR:", 6) != 0) {
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
    
    // Find the active preset
    int presetIndex = current_config.active_preset_index;
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Active preset not found");
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
    
    // Send FIR filter command to Teensy
    sendToTeensy(CMD_SET_FIR, speaker, filename);
    
    // Prepare and send response
        DynamicJsonDocument doc(1024);
    doc["status"] = "ok";
    doc["speaker"] = speaker;
    doc["filename"] = filename;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
    
    // Broadcast update
    broadcastWebSocket(response);
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
    
    // Find the active preset
    int presetIndex = current_config.active_preset_index;
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Active preset not found");
        return;
    }
    
    // Update the FIR filter enabled state
    current_config.presets[presetIndex].FIRFiltersEnabled = (state == "on");
    scheduleConfigWrite();
    
    // Send FIR enable/disable command to Teensy
    sendOnOffToTeensy(CMD_SET_FIR_ENABLED, state == "on");
    
    // Prepare and send response
        DynamicJsonDocument doc(1024);
    doc["status"] = "ok";
    doc["FIRFiltersEnabled"] = (state == "on");
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
    
    // Broadcast update
    broadcastWebSocket(response);
}