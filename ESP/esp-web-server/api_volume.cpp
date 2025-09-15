#include "api_volume.h"
#include "config.h"
#include "teensy_comm.h"
#include "utilities.h"
#include "websocket.h"
#include <ArduinoJson.h>

void handlePutVolume(AsyncWebServerRequest *request) {
    if (request->hasParam("value")) {
        int volume = request->getParam("value")->value().toInt();
        if (volume >= 0 && volume <= 100) {
            current_config.volume = volume;
            float volumeFloat = volume / 100.0f;
            sendFloatToTeensy(CMD_SET_VOLUME, volumeFloat);
            scheduleConfigWrite();
            String message = "{\"messageType\":\"volumeChanged\",\"volume\": " + String(current_config.volume) + "}";
            broadcastWebSocket(message);
            
            AsyncResponseStream *response = request->beginResponseStream("application/json");
            DynamicJsonDocument json(128);
            json["success"] = true;
            json["volume"] = current_config.volume;
            serializeJson(json, *response);
            request->send(response);
        } else {
            AsyncResponseStream *response = request->beginResponseStream("application/json");
            DynamicJsonDocument json(128);
            json["success"] = false;
            json["error"] = "Volume must be between 0 and 100";
            serializeJson(json, *response);
            response->setCode(400);
            request->send(response);
        }
    } else {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument json(128);
        json["success"] = false;
        json["error"] = "Missing value parameter";
        serializeJson(json, *response);
        response->setCode(400);
        request->send(response);
    }
}

void increase_volume() {
    if (current_config.volume < 100) {
        current_config.volume += 2;
        if (current_config.volume > 100) {
            current_config.volume = 100;
        }
        float volumeFloat = current_config.volume / 100.0f;
        sendFloatToTeensy(CMD_SET_VOLUME, volumeFloat);
        scheduleConfigWrite();
        String message = "{\"messageType\":\"volumeChanged\",\"volume\": " + String(current_config.volume) + "}";
        broadcastWebSocket(message);
    }
}

void decrease_volume() {
    if (current_config.volume > 0) {
        current_config.volume -= 2;
        if (current_config.volume < 0) {
            current_config.volume = 0;
        }
        float volumeFloat = current_config.volume / 100.0f;
        sendFloatToTeensy(CMD_SET_VOLUME, volumeFloat);
        scheduleConfigWrite();
        String message = "{\"messageType\":\"volumeChanged\",\"volume\": " + String(current_config.volume) + "}";
        broadcastWebSocket(message);
    }
}

void toggle_mute() {
    current_config.muted = !current_config.muted;
    sendOnOffToTeensy(CMD_SET_MUTE, current_config.muted);
    scheduleConfigWrite();
    String message = "{\"messageType\":\"muteChanged\",\"muted\": " + String(current_config.muted) + "}";
    broadcastWebSocket(message);
}