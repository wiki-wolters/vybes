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

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);

    broadcastWebSocket(response);
}

void handlePutToneStop(AsyncWebServerRequest *request) {
    current_config.toneFrequency = 0;
    current_config.toneVolume = 0;
    scheduleConfigWrite();

    sendToTeensy("tone_stop", "");

        DynamicJsonDocument doc(1024);
    doc["toneFrequency"] = 0;
    doc["toneVolume"] = 0;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);

    broadcastWebSocket(response);
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

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);

    broadcastWebSocket(response);
}

void handlePutPulse(AsyncWebServerRequest *request) {
    sendToTeensy("pulse", "");
    request->send(200, "text/plain", "Pulse triggered");
}