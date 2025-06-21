#include "globals.h"
#include "config.h"
#include "web_server.h"
#include "websocket.h"
#include "teensy_comm.h"
#include "utilities.h"
#include <ArduinoJson.h>

void handlePutPresetDelay(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    String speaker = request->pathArg(1);
    String delayMsStr = request->pathArg(2);
    
    if (speaker != "left" && speaker != "right" && speaker != "sub") {
        request->send(400, "text/plain", "Invalid speaker. Must be 'left', 'right', or 'sub'");
        return;
    }
    
    float delayMs = delayMsStr.toFloat();
    if (delayMs < 0 || delayMs > 100.0f) {
        request->send(400, "text/plain", "Delay must be between 0 and 100 ms");
        return;
    }
    
    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }
    
    // Update the delay in the config
    if (speaker == "left") {
        current_config.presets[presetIndex].delay.left = delayMs;
    } else if (speaker == "right") {
        current_config.presets[presetIndex].delay.right = delayMs;
    } else if (speaker == "sub") {
        current_config.presets[presetIndex].delay.sub = delayMs;
    }
    
    scheduleConfigWrite();
    
    // Send command to Teensy
    sendToTeensy(CMD_SET_DELAYS, speaker, String(delayMs, 2));
    
    // Prepare and send response
    DynamicJsonDocument doc(128);
    doc["status"] = "ok";
    doc["speaker"] = speaker;
    doc["delayMs"] = delayMs;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
    
    // Broadcast update
    broadcastWebSocket(response);
}

void handlePutPresetDelayEnabled(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    String state = request->pathArg(1);
    
    if (state != "on" && state != "off") {
        request->send(400, "text/plain", "Invalid state. Must be 'on' or 'off'");
        return;
    }
    
    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }
    
    bool enabled = (state == "on");
    
    // Update the config
    current_config.presets[presetIndex].delayEnabled = enabled;
    scheduleConfigWrite();
    
    // Send command to Teensy
    sendOnOffCommand(CMD_SET_DELAY_ENABLED, enabled);
    
    // Prepare and send response
    DynamicJsonDocument doc(128);
    doc["status"] = "ok";
    doc["delayEnabled"] = enabled;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
    
    // Broadcast update
    broadcastWebSocket(response);
}

void handleGetDelays(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    
    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }
    
    const Preset& preset = current_config.presets[presetIndex];
    
    // Prepare and send response
    DynamicJsonDocument doc(256);
    doc["status"] = "ok";
    doc["delayEnabled"] = preset.delayEnabled;
    
    JsonObject delays = doc.createNestedObject("delays");
    delays["left"] = preset.delay.left;
    delays["right"] = preset.delay.right;
    delays["sub"] = preset.delay.sub;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}
