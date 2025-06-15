#include "globals.h"
#include "config.h"
#include "web_server.h"
#include "websocket.h"
#include "file_system.h"
#include "utilities.h"

// API Handler Implementations

void handleGetCalibration(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(256);
    doc["isCalibrated"] = current_config.isCalibrated;
    if (current_config.isCalibrated) {
        doc["spl"] = current_config.calibrationSpl;
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handlePutCalibrate(AsyncWebServerRequest *request) {
    String splStr = request->pathArg(0);
    int spl = splStr.toInt();

    if (spl < 60 || spl > 100) {
        request->send(400, "text/plain", "Invalid SPL value");
        return;
    }

    current_config.calibrationSpl = spl;
    current_config.isCalibrated = true;
    scheduleConfigWrite();

    DynamicJsonDocument doc(256);
    doc["isCalibrated"] = current_config.isCalibrated;
    doc["spl"] = current_config.calibrationSpl;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);

    broadcastWebSocket(response);
}
