#include "api_gains.h"
#include "config.h"
#include "api_helpers.h"
#include "teensy_comm.h"
#include <AsyncJson.h>

void handleGetPresetGains(AsyncWebServerRequest* request) {
    String presetName = request->getParam("preset_name")->value();
    if (presetName.isEmpty()) {
        request->send(400, "application/json", "{\"error\":\"Missing preset_name\"}");
        return;
    }

    StaticJsonDocument<256> doc;
    if (config_get_preset_gains(presetName, doc)) {
        String output;
        serializeJson(doc, output);
        request->send(200, "application/json", output);
    } else {
        request->send(404, "application/json", "{\"error\":\"Preset not found\"}");
    }
}

void handleSetPresetGains(AsyncWebServerRequest* request, JsonVariant& json) {
    String presetName = request->getParam("preset_name")->value();
    if (presetName.isEmpty()) {
        request->send(400, "application/json", "{\"error\":\"Missing preset_name\"}");
        return;
    }

    JsonObject gains = json.as<JsonObject>();
    if (config_set_preset_gains(presetName, gains)) {
        request->send(200, "application/json", "{\"success\":true}");
        // It might be necessary to send the updated gains to the Teensy here if the preset is active
    } else {
        request->send(404, "application/json", "{\"error\":\"Preset not found\"}");
    }
}

void addPresetGainsHandler(AsyncWebServer* server) {
    server->on("/preset/gains", HTTP_GET, handleGetPresetGains);
    server->addHandler(new AsyncCallbackJsonWebHandler("/preset/gains", handleSetPresetGains));
}

void handlePutSpeakerGain(AsyncWebServerRequest* request) {
    if (request->hasParam("speaker") && request->hasParam("value")) {
        String speaker = request->getParam("speaker")->value();
        float value = request->getParam("value")->value().toFloat();

        if (speaker.equalsIgnoreCase("left")) {
            current_config.speakerGains.left = value;
        } else if (speaker.equalsIgnoreCase("right")) {
            current_config.speakerGains.right = value;
        } else if (speaker.equalsIgnoreCase("sub")) {
            current_config.speakerGains.sub = value;
        } else {
            request->send(400, "application/json", "{\"error\":\"Invalid speaker\"}");
            return;
        }

        save_config();
        sendToTeensy(CMD_SET_SPEAKER_GAINS, String(current_config.speakerGains.left, 2), String(current_config.speakerGains.right, 2), String(current_config.speakerGains.sub, 2));
        request->send(200, "application/json", "{\"success\":true}");
    } else {
        // Fallback to original JSON body handling
        if (!request->hasArg("plain")) {
            request->send(400, "application/json", "{\"error\":\"Missing JSON body or speaker/value parameters\"}");
            return;
        }

        DynamicJsonDocument doc(256);
        DeserializationError error = deserializeJson(doc, request->arg("plain"));

        if (error) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        float left = (doc["left"] | current_config.speakerGains.left * 100.0f) / 100.0f;
        float right = (doc["right"] | current_config.speakerGains.right * 100.0f) / 100.0f;
        float sub = (doc["sub"] | current_config.speakerGains.sub * 100.0f) / 100.0f;

        current_config.speakerGains.left = left;
        current_config.speakerGains.right = right;
        current_config.speakerGains.sub = sub;

        save_config();
        sendToTeensy(CMD_SET_SPEAKER_GAINS, String(left, 2), String(right, 2), String(sub, 2));
        request->send(200, "application/json", "{\"success\":true}");
    }
}

void handlePutInputGains(AsyncWebServerRequest* request, JsonVariant& json) {
    JsonObject gains = json.as<JsonObject>();

    float spdif = gains["spdif"] | current_config.inputGains.spdif;
    float bluetooth = gains["bluetooth"] | current_config.inputGains.bluetooth;
    float usb = gains["usb"] | current_config.inputGains.usb;
    float tone = gains["tone"] | current_config.inputGains.tone;

    current_config.inputGains.spdif = spdif;
    current_config.inputGains.bluetooth = bluetooth;
    current_config.inputGains.usb = usb;
    current_config.inputGains.tone = tone;

    save_config();
    sendToTeensy(CMD_SET_INPUT_GAINS, String(bluetooth, 2), String(spdif, 2), String(usb, 2), String(tone, 2));
    request->send(200, "application/json", "{\"success\":true}");
}
