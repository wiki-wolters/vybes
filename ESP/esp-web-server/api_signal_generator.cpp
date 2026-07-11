#include "globals.h"
#include "config.h"
#include "web_server.h"
#include "websocket.h"
#include "teensy_comm.h"
#include "utilities.h"

esp_err_t handlePutTone(PsychicRequest *request) {
    if (!request->hasParam("frequency") || !request->hasParam("volume")) {
        return request->reply(400, "text/plain", "Missing required parameters");
    }
    String freqStr = request->getParam("frequency")->value();
    String volStr = request->getParam("volume")->value();
    int freq = freqStr.toInt();
    int vol = volStr.toInt();

    if (freq < 20 || freq > 20000) {
        return request->reply(400, "text/plain", "Invalid frequency value");
    }
    if (vol < 0 || vol > 100) {
        return request->reply(400, "text/plain", "Invalid volume value");
    }

    current_config.toneFrequency = freq;
    current_config.toneVolume = vol;
    scheduleConfigWrite();

    sendToTeensy(CMD_SET_TONE, freqStr, volStr);

        DynamicJsonDocument doc(1024);
    doc["toneFrequency"] = current_config.toneFrequency;
    doc["toneVolume"] = current_config.toneVolume;

    char responseBuffer[1024]; // Adjust size as needed
    size_t len = serializeJson(doc, responseBuffer, sizeof(responseBuffer));
    if (len > 0 && len < sizeof(responseBuffer)) {
        broadcastWebSocket(responseBuffer);
        return request->reply(200, "application/json", responseBuffer);
    }
    DebugSerial.println("Error serializing JSON for WebSocket broadcast or buffer too small.");
    return request->reply(500, "application/json", "{\"error\":\"Failed to serialize JSON response or buffer too small\"}");
}

esp_err_t handlePutToneStop(PsychicRequest *request) {
    current_config.toneFrequency = 0;
    current_config.toneVolume = 0;
    scheduleConfigWrite();

    sendToTeensy(CMD_STOP_TONE, "");

        DynamicJsonDocument doc(1024);
    doc["toneFrequency"] = 0;
    doc["toneVolume"] = 0;

    char responseBuffer[1024]; // Adjust size as needed
    size_t len = serializeJson(doc, responseBuffer, sizeof(responseBuffer));
    if (len > 0 && len < sizeof(responseBuffer)) {
        broadcastWebSocket(responseBuffer);
        return request->reply(200, "application/json", responseBuffer);
    }
    DebugSerial.println("Error serializing JSON for WebSocket broadcast or buffer too small.");
    return request->reply(500, "application/json", "{\"error\":\"Failed to serialize JSON response or buffer too small\"}");
}

esp_err_t handlePutNoise(PsychicRequest *request) {
    if (!request->hasParam("level")) {
        return request->reply(400, "text/plain", "Missing required parameters");
    }
    String volStr = request->getParam("level")->value();
    int vol = volStr.toInt();

    if (vol < 0 || vol > 100) {
        return request->reply(400, "text/plain", "Invalid volume value");
    }

    current_config.noiseVolume = vol;
    scheduleConfigWrite();

    sendToTeensy(CMD_SET_NOISE, volStr);

        DynamicJsonDocument doc(1024);
    doc["noiseVolume"] = current_config.noiseVolume;

    char responseBuffer[1024]; // Adjust size as needed
    size_t len = serializeJson(doc, responseBuffer, sizeof(responseBuffer));
    if (len > 0 && len < sizeof(responseBuffer)) {
        broadcastWebSocket(responseBuffer);
        return request->reply(200, "application/json", responseBuffer);
    }
    DebugSerial.println("Error serializing JSON for WebSocket broadcast or buffer too small.");
    return request->reply(500, "application/json", "{\"error\":\"Failed to serialize JSON response or buffer too small\"}");
}
