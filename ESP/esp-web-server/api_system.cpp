#include "globals.h"
#include "config.h"
#include "web_server.h"
#include "websocket.h"
#include "teensy_comm.h"
#include "utilities.h"

void handleGetStatus(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(1024);

    JsonObject speakerGains = doc.createNestedObject("speakerGains");
    speakerGains["left"] = current_config.speakerGains.left;
    speakerGains["right"] = current_config.speakerGains.right;
    speakerGains["sub"] = current_config.speakerGains.sub;
    
    JsonObject inputGains = doc.createNestedObject("inputGains");
    inputGains["spdif"] = current_config.inputGains.spdif;
    inputGains["bluetooth"] = current_config.inputGains.bluetooth;
    
    JsonObject mute = doc.createNestedObject("mute");
    mute["state"] = current_config.muteState;
    mute["percent"] = current_config.mutePercent;
    
    JsonObject tone = doc.createNestedObject("tone");
    tone["frequency"] = current_config.toneFrequency;
    tone["volume"] = current_config.toneVolume;
    
    JsonObject noise = doc.createNestedObject("noise");
    noise["volume"] = current_config.noiseVolume;
    
    doc["currentPreset"] = current_config.presets[current_config.active_preset_index].name;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handlePutMute(AsyncWebServerRequest *request) {
    String state = request->pathArg(0);
    if (state != "on" && state != "off") {
        request->send(400, "text/plain", "Invalid state");
        return;
    }

    strncpy(current_config.muteState, state.c_str(), sizeof(current_config.muteState) - 1);
    current_config.muteState[sizeof(current_config.muteState) - 1] = '\0'; // Ensure null termination
    scheduleConfigWrite();

    sendOnOffToTeensy(CMD_SET_MUTE, state == "on");

    DynamicJsonDocument doc(256);
    doc["muteState"] = current_config.muteState;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);

    broadcastWebSocket(response);
}

void handlePutMutePercent(AsyncWebServerRequest *request) {
    String percentStr = request->pathArg(0);
    int percent = percentStr.toInt();

    if (percent < 0 || percent > 100) {
        request->send(400, "text/plain", "Invalid percent value");
        return;
    }

    current_config.mutePercent = percent;
    scheduleConfigWrite();

    sendFloatToTeensy(CMD_SET_MUTE_PERCENT, percent);

    DynamicJsonDocument doc(256);
    doc["mutePercent"] = current_config.mutePercent;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);

    broadcastWebSocket(response);
}
