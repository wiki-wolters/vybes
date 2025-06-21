#include "globals.h"
#include "config.h"
#include "web_server.h"
#include "websocket.h"
#include "teensy_comm.h"
#include "utilities.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

void handleGetFirFiles(AsyncWebServerRequest *request) {
    // TODO: request a list of all files in the root directory of the SD card from Teensy
    // and return it as a JSON array

    request->send(200, "application/json", "[]");
}

void handlePutPresetFir(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    String speaker = request->pathArg(1);
    String filename = request->pathArg(2);
    
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
    
    // Send command to Teensy to load the FIR filter
    String command = "load_fir " + speaker + " " + filename;
    sendToTeensy(command.c_str(), "");
    
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

void handlePutPresetFirEnabled(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    String state = request->pathArg(1);
    // Find the active preset
    int presetIndex = current_config.active_preset_index;
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Active preset not found");
        return;
    }

    if (state != "on" && state != "off") {
        request->send(400, "text/plain", "Invalid state");
        return;
    }
    
    // Update the FIR filter filename for the specified speaker
    Preset* preset = &current_config.presets[presetIndex];
    if (state == "on") {
        preset->FIRFiltersEnabled = true;
    } else {
        preset->FIRFiltersEnabled = false;
    }
    
    scheduleConfigWrite();
    
    // Send command to Teensy to load the FIR filter
    String command = "fir_enabled " + state;
    sendToTeensy(command.c_str(), "");
    
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

