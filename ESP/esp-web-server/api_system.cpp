#include "globals.h"
#include "config.h"
#include "web_server.h"
#include "websocket.h"
#include "teensy_comm.h"
#include "utilities.h"

void handleGetStatus(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(1024);
    doc["subwooferState"] = current_config.subwooferState;
    doc["bypassState"] = current_config.bypassState;
    doc["muteState"] = current_config.muteState;
    doc["mutePercent"] = current_config.mutePercent;
    doc["toneFrequency"] = current_config.toneFrequency;
    doc["toneVolume"] = current_config.toneVolume;
    doc["noiseVolume"] = current_config.noiseVolume;
    doc["currentPreset"] = current_config.currentPresetName;

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

    strncpy(current_config.subwooferState, state.c_str(), sizeof(current_config.subwooferState) - 1);
    current_config.subwooferState[sizeof(current_config.subwooferState) - 1] = '\0'; // Ensure null termination
    scheduleConfigWrite();

    sendToTeensy("sub", state);

    DynamicJsonDocument doc(256);
    doc["subwooferState"] = current_config.subwooferState;

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

    strncpy(current_config.bypassState, state.c_str(), sizeof(current_config.bypassState) - 1);
    current_config.bypassState[sizeof(current_config.bypassState) - 1] = '\0'; // Ensure null termination
    scheduleConfigWrite();

    sendToTeensy("bypass", state);

    DynamicJsonDocument doc(256);
    doc["bypassState"] = current_config.bypassState;

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

    strncpy(current_config.muteState, state.c_str(), sizeof(current_config.muteState) - 1);
    current_config.muteState[sizeof(current_config.muteState) - 1] = '\0'; // Ensure null termination
    scheduleConfigWrite();

    sendToTeensy("mute", state);

    DynamicJsonDocument doc(256);
    doc["muteState"] = current_config.muteState;

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

    current_config.mutePercent = percent;
    scheduleConfigWrite();

    sendToTeensy("mute_percent", percentStr);

    DynamicJsonDocument doc(256);
    doc["mutePercent"] = current_config.mutePercent;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);

    broadcastWebSocket(response);
}
