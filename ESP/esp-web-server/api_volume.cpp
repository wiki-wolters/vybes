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
            // Prepare data for WebSocket broadcast
            DynamicJsonDocument doc(128);
            doc["messageType"] = "volumeChanged";
            doc["volume"] = current_config.volume;
            char messageBuffer[128]; // Adjust size as needed
            size_t len = serializeJson(doc, messageBuffer, sizeof(messageBuffer));
            if (len > 0 && len < sizeof(messageBuffer)) {
                broadcastWebSocket(messageBuffer);
            } else {
                Serial.println("Error serializing JSON for WebSocket broadcast or buffer too small.");
            }
            
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

void increase_volume(int amount) {
    if (current_config.volume < 100) {
        current_config.volume += amount;
        if (current_config.volume > 100) {
            current_config.volume = 100;
        }
        float volumeFloat = current_config.volume / 100.0f;
        sendFloatToTeensy(CMD_SET_VOLUME, volumeFloat);
        scheduleConfigWrite();
        // Prepare data for WebSocket broadcast
        DynamicJsonDocument doc(128);
        doc["messageType"] = "volumeChanged";
        doc["volume"] = current_config.volume;
        char messageBuffer[128]; // Adjust size as needed
        size_t len = serializeJson(doc, messageBuffer, sizeof(messageBuffer));
        if (len > 0 && len < sizeof(messageBuffer)) {
            broadcastWebSocket(messageBuffer);
        } else {
            Serial.println("Error serializing JSON for WebSocket broadcast or buffer too small.");
        }
    }
}

void decrease_volume(int amount) {
    if (current_config.volume > 0) {
        current_config.volume -= amount;
        if (current_config.volume < 0) {
            current_config.volume = 0;
        }
        float volumeFloat = current_config.volume / 100.0f;
        sendFloatToTeensy(CMD_SET_VOLUME, volumeFloat);
        scheduleConfigWrite();
        // Prepare data for WebSocket broadcast
        DynamicJsonDocument doc(128);
        doc["messageType"] = "volumeChanged";
        doc["volume"] = current_config.volume;
        char messageBuffer[128]; // Adjust size as needed
        size_t len = serializeJson(doc, messageBuffer, sizeof(messageBuffer));
        if (len > 0 && len < sizeof(messageBuffer)) {
            broadcastWebSocket(messageBuffer);
        } else {
            Serial.println("Error serializing JSON for WebSocket broadcast or buffer too small.");
        }
    }
}

void toggle_mute() {
    current_config.muted = !current_config.muted;
    sendOnOffToTeensy(CMD_SET_MUTE, current_config.muted);
    scheduleConfigWrite();
    // Prepare data for WebSocket broadcast
    DynamicJsonDocument doc(128);
    doc["messageType"] = "muteChanged";
    doc["muted"] = current_config.muted;
    char messageBuffer[128]; // Adjust size as needed
    size_t len = serializeJson(doc, messageBuffer, sizeof(messageBuffer));
    if (len > 0 && len < sizeof(messageBuffer)) {
        broadcastWebSocket(messageBuffer);
    } else {
        Serial.println("Error serializing JSON for WebSocket broadcast or buffer too small.");
    }
}
