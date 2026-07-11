#include "globals.h"
#include "config.h"
#include "web_server.h"
#include "websocket.h"
#include "teensy_comm.h"
#include "utilities.h"
#include "api_helpers.h"

esp_err_t handleGetStatus(PsychicRequest *request) {
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
    return request->reply(200, "application/json", response.c_str());
}

esp_err_t handlePutMute(PsychicRequest *request) {
    if (!request->hasParam("state")) {
        return request->reply(400, "text/plain", "Missing required parameters");
    }
    String state = request->getParam("state")->value();
    
    DebugSerial.print("Put Mute: ");DebugSerial.println(state);

    if (state != "on" && state != "off") {
        return request->reply(400, "text/plain", "Invalid state");
    }

    current_config.muted = (state == "on");
    scheduleConfigWrite();

    sendOnOffToTeensy(CMD_SET_MUTE, current_config.muted);

    // Same shape as the broadcast sent by toggle_mute (IR remote path)
    StaticJsonDocument<96> doc;
    doc["messageType"] = "muteChanged";
    doc["muted"] = current_config.muted;
    return sendJsonAndBroadcast(request, doc);
}

esp_err_t handlePutMutePercent(PsychicRequest *request) {
    if (!request->hasParam("percent")) {
        return request->reply(400, "text/plain", "Missing required parameters");
    }
    String percentStr = request->getParam("percent")->value();
    int percent = percentStr.toInt();

    if (percent < 0 || percent > 100) {
        return request->reply(400, "text/plain", "Invalid percent value");
    }

    current_config.mutePercent = percent;
    scheduleConfigWrite();

    sendFloatToTeensy(CMD_SET_MUTE_PERCENT, percent);

    StaticJsonDocument<96> doc;
    doc["messageType"] = "mutePercentChanged";
    doc["mutePercent"] = current_config.mutePercent;
    return sendJsonAndBroadcast(request, doc);
}