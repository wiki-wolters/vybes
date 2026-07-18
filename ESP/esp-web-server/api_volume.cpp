#include "globals.h"
#include "api_volume.h"
#include "config.h"
#include "teensy_comm.h"
#include "utilities.h"
#include "websocket.h"
#include <ArduinoJson.h>

esp_err_t handlePutVolume(PsychicRequest *request) {
    if (!request->hasParam("value")) {
        return request->reply(400, "application/json",
                              "{\"success\":false,\"error\":\"Missing value parameter\"}");
    }

    int volume = request->getParam("value")->value().toInt();
    if (volume < 0 || volume > 100) {
        return request->reply(400, "application/json",
                              "{\"success\":false,\"error\":\"Volume must be between 0 and 100\"}");
    }

    {
        ConfigLock lock;
        current_config.volume = volume;
        scheduleConfigWrite();
    }
    float volumeFloat = volume / 100.0f;
    sendFloatToTeensy(CMD_SET_VOLUME, volumeFloat);

    // Prepare data for WebSocket broadcast
    JsonDocument doc;
    doc["messageType"] = "volumeChanged";
    doc["volume"] = current_config.volume;
    char messageBuffer[128];
    size_t len = serializeJson(doc, messageBuffer, sizeof(messageBuffer));
    if (len > 0 && len < sizeof(messageBuffer)) {
        broadcastWebSocket(messageBuffer);
    } else {
        DebugSerial.println("Error serializing JSON for WebSocket broadcast or buffer too small.");
    }

    char response[64];
    snprintf(response, sizeof(response), "{\"success\":true,\"volume\":%d}", current_config.volume);
    return request->reply(200, "application/json", response);
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
        JsonDocument doc;
        doc["messageType"] = "volumeChanged";
        doc["volume"] = current_config.volume;
        char messageBuffer[128]; // Adjust size as needed
        size_t len = serializeJson(doc, messageBuffer, sizeof(messageBuffer));
        if (len > 0 && len < sizeof(messageBuffer)) {
            broadcastWebSocket(messageBuffer);
        } else {
            DebugSerial.println("Error serializing JSON for WebSocket broadcast or buffer too small.");
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
        JsonDocument doc;
        doc["messageType"] = "volumeChanged";
        doc["volume"] = current_config.volume;
        char messageBuffer[128]; // Adjust size as needed
        size_t len = serializeJson(doc, messageBuffer, sizeof(messageBuffer));
        if (len > 0 && len < sizeof(messageBuffer)) {
            broadcastWebSocket(messageBuffer);
        } else {
            DebugSerial.println("Error serializing JSON for WebSocket broadcast or buffer too small.");
        }
    }
}

void toggle_mute() {
    current_config.muted = !current_config.muted;
    sendOnOffToTeensy(CMD_SET_MUTE, current_config.muted);
    scheduleConfigWrite();
    // Prepare data for WebSocket broadcast
    JsonDocument doc;
    doc["messageType"] = "muteChanged";
    doc["muted"] = current_config.muted;
    char messageBuffer[128]; // Adjust size as needed
    size_t len = serializeJson(doc, messageBuffer, sizeof(messageBuffer));
    if (len > 0 && len < sizeof(messageBuffer)) {
        broadcastWebSocket(messageBuffer);
    } else {
        DebugSerial.println("Error serializing JSON for WebSocket broadcast or buffer too small.");
    }
}
