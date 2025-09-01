#include "api_volume.h"
#include "config.h"
#include "teensy_comm.h"
#include "utilities.h"

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
            request->send(200, "text/plain", "Volume updated");
        } else {
            request->send(400, "text/plain", "Volume must be between 0 and 100");
        }
    } else {
        request->send(400, "text/plain", "Missing value parameter");
    }
}