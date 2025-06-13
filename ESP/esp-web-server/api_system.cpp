#include "globals.h"
#include "web_server.h"
#include "websocket.h"
#include "teensy_comm.h"
#include "utilities.h"

void handleGetStatus(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(1024);
    doc["subwooferState"] = systemSettings.subwooferState;
    doc["bypassState"] = systemSettings.bypassState;
    doc["muteState"] = systemSettings.muteState;
    doc["mutePercent"] = systemSettings.mutePercent;
    doc["toneFrequency"] = systemSettings.toneFrequency;
    doc["toneVolume"] = systemSettings.toneVolume;
    doc["noiseVolume"] = systemSettings.noiseVolume;
    doc["currentPreset"] = systemSettings.currentPreset;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handlePutSub(AsyncWebServerRequest *request) {
    String state = request->pathArg(0);
    if (state != "on" && state != "off") {
        request->send(400, "text/plain", "Invalid state");
        return;
    }

    systemSettings.subwooferState = state;
    scheduleConfigWrite();

    sendToTeensy("sub", state);

    DynamicJsonDocument doc(256);
    doc["subwooferState"] = systemSettings.subwooferState;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);

    broadcastWebSocket(response);
}

void handlePutBypass(AsyncWebServerRequest *request) {
    String state = request->pathArg(0);
    if (state != "on" && state != "off") {
        request->send(400, "text/plain", "Invalid state");
        return;
    }

    systemSettings.bypassState = state;
    scheduleConfigWrite();

    sendToTeensy("bypass", state);

    DynamicJsonDocument doc(256);
    doc["bypassState"] = systemSettings.bypassState;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);

    broadcastWebSocket(response);
}

void handlePutMute(AsyncWebServerRequest *request) {
    String state = request->pathArg(0);
    if (state != "on" && state != "off") {
        request->send(400, "text/plain", "Invalid state");
        return;
    }

    systemSettings.muteState = state;
    scheduleConfigWrite();

    sendToTeensy("mute", state);

    DynamicJsonDocument doc(256);
    doc["muteState"] = systemSettings.muteState;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);

    broadcastWebSocket(response);
}

void handlePutMutePercent(AsyncWebServerRequest *request) {
    String percentStr = request->pathArg(0);
    int percent = percentStr.toInt();

    if (percent < 0 || percent > 100) {
        request->send(400, "text/plain", "Invalid percent value");
        return;
    }

    systemSettings.mutePercent = percent;
    scheduleConfigWrite();

    sendToTeensy("mute_percent", percentStr);

    DynamicJsonDocument doc(256);
    doc["mutePercent"] = systemSettings.mutePercent;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);

    broadcastWebSocket(response);
}
