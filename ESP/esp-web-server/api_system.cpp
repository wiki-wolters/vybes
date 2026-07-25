#include "globals.h"
#include "config.h"
#include "web_server.h"
#include "websocket.h"
#include "teensy_comm.h"
#include "utilities.h"
#include "api_helpers.h"

esp_err_t handleGetStatus(PsychicRequest *request) {
    JsonDocument doc;

    JsonObject speakerGains = doc.createNestedObject("speakerGains");
    speakerGains["left"] = current_config.speakerGains.left * 100.0f;
    speakerGains["right"] = current_config.speakerGains.right * 100.0f;
    speakerGains["sub"] = current_config.speakerGains.sub * 100.0f;
    
    JsonObject inputGains = doc.createNestedObject("inputGains");
    inputGains["spdif"] = current_config.inputGains.spdif;
    inputGains["bluetooth"] = current_config.inputGains.bluetooth;
    inputGains["usb"] = current_config.inputGains.usb;
    inputGains["tone"] = current_config.inputGains.tone;
    inputGains["analog"] = current_config.inputGains.analog;
    
    JsonObject mute = doc.createNestedObject("mute");
    mute["muted"] = current_config.muted;
    mute["percent"] = current_config.mutePercent;
    
    JsonObject tone = doc.createNestedObject("tone");
    tone["frequency"] = current_config.toneFrequency;
    tone["volume"] = current_config.toneVolume;
    
    JsonObject noise = doc.createNestedObject("noise");
    noise["volume"] = current_config.noiseVolume;
    
    doc["currentPreset"] = current_config.presets[current_config.active_preset_index].name;
    doc["deviceName"] = current_config.deviceName;

    // Add master volume
    doc["volume"] = current_config.volume; // Add this line

    String response;
    serializeJson(doc, response);
    return request->reply(200, "application/json", response.c_str());
}

// PUT /device/name?name= - rename the device so several Vybes units can
// share one network. Applies to mDNS immediately; the standalone-AP SSID
// and setup-portal name pick it up on the next boot. The TLS certificate
// must be re-issued for the new hostname (ESP/make-certs.sh <name>.local)
// or HTTPS clients will see a name mismatch.
esp_err_t handlePutDeviceName(PsychicRequest *request) {
    if (!request->hasParam("name")) {
        return request->reply(400, "text/plain", "Missing name parameter");
    }
    String name = request->getParam("name")->value();

    if (!is_valid_device_name(name.c_str())) {
        return request->reply(400, "text/plain",
            "Device name must be 1-24 characters of lowercase letters, digits and dashes, not starting or ending with a dash");
    }

    {
        ConfigLock lock;
        strlcpy(current_config.deviceName, name.c_str(), sizeof(current_config.deviceName));
        scheduleConfigWrite();
    }

    // Re-announce "<name>.local" right away
    startMdns();

    JsonDocument doc;
    doc["messageType"] = "deviceNameChanged";
    doc["deviceName"] = current_config.deviceName;
    return sendJsonAndBroadcast(request, doc);
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

    {
        ConfigLock lock;
        current_config.muted = (state == "on");
        scheduleConfigWrite();
    }

    sendOnOffToTeensy(CMD_SET_MUTE, current_config.muted);

    // Same shape as the broadcast sent by toggle_mute (IR remote path)
    JsonDocument doc;
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

    {
        ConfigLock lock;
        current_config.mutePercent = percent;
        scheduleConfigWrite();
    }

    sendFloatToTeensy(CMD_SET_MUTE_PERCENT, percent);

    JsonDocument doc;
    doc["messageType"] = "mutePercentChanged";
    doc["mutePercent"] = current_config.mutePercent;
    return sendJsonAndBroadcast(request, doc);
}