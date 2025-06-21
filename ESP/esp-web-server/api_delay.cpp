#include "globals.h"
#include "config.h"
#include "web_server.h"
#include "websocket.h"
#include "teensy_comm.h"
#include "utilities.h"
#include <ArduinoJson.h>

void handlePutPresetDelayNamed(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    String speaker = request->pathArg(1);
    String delayUsStr = request->pathArg(2);
    
    if (speaker != "left" && speaker != "right" && speaker != "sub") {
        request->send(400, "text/plain", "Invalid speaker. Must be 'left', 'right', or 'sub'");
        return;
    }
    
    float delayUs = delayUsStr.toFloat();
    if (delayUs < 0 || delayUs > 10000.0f) {
        request->send(400, "text/plain", "Delay must be between 0 and 10000 us");
        return;
    }
    
    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }
    
    // Update the delay in the config
    if (speaker == "left") {
        current_config.presets[presetIndex].delay.left = delayUs;
    } else if (speaker == "right") {
        current_config.presets[presetIndex].delay.right = delayUs;
    } else if (speaker == "sub") {
        current_config.presets[presetIndex].delay.sub = delayUs;
    }
    
    scheduleConfigWrite();
    
    // Send command to Teensy
    sendToTeensy(CMD_SET_DELAYS, 
        String((int)current_config.presets[presetIndex].delay.left),
        String((int)current_config.presets[presetIndex].delay.right),
        String((int)current_config.presets[presetIndex].delay.sub));
    
    // Prepare and send response
    DynamicJsonDocument doc(128);
    doc["status"] = "ok";
    doc["speaker"] = speaker;
    doc["delayUs"] = delayUs;
    
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
