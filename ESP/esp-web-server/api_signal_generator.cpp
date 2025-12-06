#include "globals.h"
#include "config.h"
#include "web_server.h"
#include "websocket.h"
#include "teensy_comm.h"
#include "utilities.h"

void handlePutTone(AsyncWebServerRequest *request) {
    if (!request->hasParam("frequency") || !request->hasParam("volume")) {
        request->send(400, "text/plain", "Missing required parameters");
        return;
    }
    String freqStr = request->getParam("frequency")->value();
    String volStr = request->getParam("volume")->value();
    int freq = freqStr.toInt();
    int vol = volStr.toInt();

    if (freq < 20 || freq > 20000) {
        request->send(400, "text/plain", "Invalid frequency value");
        return;
    }
    if (vol < 0 || vol > 100) {
        request->send(400, "text/plain", "Invalid volume value");
        return;
    }

    current_config.toneFrequency = freq;
    current_config.toneVolume = vol;
    scheduleConfigWrite();

    sendToTeensy("tone", freqStr + "," + volStr);

        DynamicJsonDocument doc(1024);
    doc["toneFrequency"] = current_config.toneFrequency;
    doc["toneVolume"] = current_config.toneVolume;

    char responseBuffer[1024]; // Adjust size as needed
    size_t len = serializeJson(doc, responseBuffer, sizeof(responseBuffer));
    if (len > 0 && len < sizeof(responseBuffer)) {
        request->send(200, "application/json", responseBuffer);
        broadcastWebSocket(responseBuffer);
    } else {
        request->send(500, "application/json", "{\"error\":\"Failed to serialize JSON response or buffer too small\"}");
        Serial.println("Error serializing JSON for WebSocket broadcast or buffer too small.");
    }
}

void handlePutToneStop(AsyncWebServerRequest *request) {
    current_config.toneFrequency = 0;
    current_config.toneVolume = 0;
    scheduleConfigWrite();

    sendToTeensy("tone_stop", "");

        DynamicJsonDocument doc(1024);
    doc["toneFrequency"] = 0;
    doc["toneVolume"] = 0;

    char responseBuffer[1024]; // Adjust size as needed
    size_t len = serializeJson(doc, responseBuffer, sizeof(responseBuffer));
    if (len > 0 && len < sizeof(responseBuffer)) {
        request->send(200, "application/json", responseBuffer);
        broadcastWebSocket(responseBuffer);
    } else {
        request->send(500, "application/json", "{\"error\":\"Failed to serialize JSON response or buffer too small\"}");
        Serial.println("Error serializing JSON for WebSocket broadcast or buffer too small.");
    }
}

void handlePutNoise(AsyncWebServerRequest *request) {
    if (!request->hasParam("level")) {
        request->send(400, "text/plain", "Missing required parameters");
        return;
    }
    String volStr = request->getParam("level")->value();
    int vol = volStr.toInt();

    if (vol < 0 || vol > 100) {
        request->send(400, "text/plain", "Invalid volume value");
        return;
    }

    current_config.noiseVolume = vol;
    scheduleConfigWrite();

    sendToTeensy("noise", volStr);

        DynamicJsonDocument doc(1024);
    doc["noiseVolume"] = current_config.noiseVolume;

    char responseBuffer[1024]; // Adjust size as needed
    size_t len = serializeJson(doc, responseBuffer, sizeof(responseBuffer));
    if (len > 0 && len < sizeof(responseBuffer)) {
        request->send(200, "application/json", responseBuffer);
        broadcastWebSocket(responseBuffer);
    } else {
        request->send(500, "application/json", "{\"error\":\"Failed to serialize JSON response or buffer too small\"}");
        Serial.println("Error serializing JSON for WebSocket broadcast or buffer too small.");
    }
}

void handlePutPulse(AsyncWebServerRequest *request) {
    sendToTeensy("pulse", "");
    request->send(200, "text/plain", "Pulse triggered");
}