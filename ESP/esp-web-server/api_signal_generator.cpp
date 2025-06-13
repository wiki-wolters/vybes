#include "globals.h"
#include "web_server.h"
#include "websocket.h"
#include "teensy_comm.h"
#include "utilities.h"

void handlePutTone(AsyncWebServerRequest *request) {
    String freqStr = request->pathArg(0);
    String volStr = request->pathArg(1);
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

    systemSettings.toneFrequency = freq;
    systemSettings.toneVolume = vol;
    scheduleConfigWrite();

    sendToTeensy("tone", freqStr + "," + volStr);

    DynamicJsonDocument doc(256);
    doc["toneFrequency"] = systemSettings.toneFrequency;
    doc["toneVolume"] = systemSettings.toneVolume;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);

    broadcastWebSocket(response);
}

void handlePutToneStop(AsyncWebServerRequest *request) {
    systemSettings.toneFrequency = 0;
    systemSettings.toneVolume = 0;
    scheduleConfigWrite();

    sendToTeensy("tone_stop", "");

    DynamicJsonDocument doc(256);
    doc["toneFrequency"] = 0;
    doc["toneVolume"] = 0;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);

    broadcastWebSocket(response);
}

void handlePutNoise(AsyncWebServerRequest *request) {
    String volStr = request->pathArg(0);
    int vol = volStr.toInt();

    if (vol < 0 || vol > 100) {
        request->send(400, "text/plain", "Invalid volume value");
        return;
    }

    systemSettings.noiseVolume = vol;
    scheduleConfigWrite();

    sendToTeensy("noise", volStr);

    DynamicJsonDocument doc(256);
    doc["noiseVolume"] = systemSettings.noiseVolume;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);

    broadcastWebSocket(response);
}

void handlePutPulse(AsyncWebServerRequest *request) {
    sendToTeensy("pulse", "");
    request->send(200, "text/plain", "Pulse triggered");
}
