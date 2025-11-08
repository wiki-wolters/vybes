#include "globals.h"
#include "config.h"
#include "web_server.h"
#include "websocket.h"
#include "teensy_comm.h"
#include "utilities.h"

void handleGetStatus(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(1024);

    JsonObject speakerGains = doc.createNestedObject("speakerGains");
    speakerGains["left"] = current_config.speakerGains.left * 100.0f;
    speakerGains["right"] = current_config.speakerGains.right * 100.0f;
    speakerGains["sub"] = current_config.speakerGains.sub * 100.0f;
    
    JsonObject inputGains = doc.createNestedObject("inputGains");
    inputGains["spdif"] = current_config.inputGains.spdif;
    inputGains["bluetooth"] = current_config.inputGains.bluetooth;
    
    JsonObject mute = doc.createNestedObject("mute");
    mute["muted"] = current_config.muted;
    mute["percent"] = current_config.mutePercent;
    
    JsonObject tone = doc.createNestedObject("tone");
    tone["frequency"] = current_config.toneFrequency;
    tone["volume"] = current_config.toneVolume;
    
    JsonObject noise = doc.createNestedObject("noise");
    noise["volume"] = current_config.noiseVolume;
    
    doc["currentPreset"] = current_config.presets[current_config.active_preset_index].name;
    
    // Add master volume
    doc["volume"] = current_config.volume; // Add this line

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handlePutMute(AsyncWebServerRequest *request) {
    if (!request->hasParam("state")) {
        request->send(400, "text/plain", "Missing required parameters");
        return;
    }
    String state = request->getParam("state")->value();
    
    Serial.print("Put Mute: ");Serial.println(state);

    if (state != "on" && state != "off") {
        request->send(400, "text/plain", "Invalid state");
        return;
    }

    current_config.muted = (state == "on");
    scheduleConfigWrite();

    sendOnOffToTeensy(CMD_SET_MUTE, current_config.muted);

        DynamicJsonDocument doc(1024);
    doc["muted"] = current_config.muted;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);

    broadcastWebSocket(response);
}

void handlePutMutePercent(AsyncWebServerRequest *request) {
    if (!request->hasParam("percent")) {
        request->send(400, "text/plain", "Missing required parameters");
        return;
    }
    String percentStr = request->getParam("percent")->value();
    int percent = percentStr.toInt();

    if (percent < 0 || percent > 100) {
        request->send(400, "text/plain", "Invalid percent value");
        return;
    }

    current_config.mutePercent = percent;
    scheduleConfigWrite();

    sendFloatToTeensy(CMD_SET_MUTE_PERCENT, percent);

        DynamicJsonDocument doc(1024);
    doc["mutePercent"] = current_config.mutePercent;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);

    broadcastWebSocket(response);
}