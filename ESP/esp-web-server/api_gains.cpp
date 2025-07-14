#include "globals.h"
#include "config.h"
#include "web_server.h"
#include "websocket.h"
#include "teensy_comm.h"
#include "utilities.h"

void handlePutSpeakerGain(AsyncWebServerRequest *request) {
    String speaker = request->pathArg(0);
    float gain = request->pathArg(1).toFloat();

    if (gain < 0.0f || gain > 2.0f) {
        request->send(400, "text/plain", "Gain must be between 0.0 and 2.0");
        return;
    }

    // Update the appropriate gain value in the config
    if (speaker == "left") {
        current_config.speakerGains.left = gain;
    } else if (speaker == "right") {
        current_config.speakerGains.right = gain;
    } else if (speaker == "sub") {
        current_config.speakerGains.sub = gain;
    }
    
    // Save the updated configuration
    scheduleConfigWrite();
    
    // Send command to Teensy using the new command structure
    sendToTeensy(CMD_SET_SPEAKER_GAINS, 
        String(current_config.speakerGains.left, 2),
        String(current_config.speakerGains.right, 2), 
        String(current_config.speakerGains.sub, 2));
    
    // Prepare and send response
    DynamicJsonDocument doc(256);
    doc[speaker + "Gain"] = gain;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
    
    // Broadcast the update to all WebSocket clients
    broadcastWebSocket(response);
}

void handlePutInputGains(AsyncWebServerRequest *request, JsonVariant &json) {
    JsonObject jsonObj = json.as<JsonObject>();

    float bluetooth = jsonObj["bluetooth"];
    float spdif = jsonObj["spdif"];
    float tone = jsonObj["tone"];

    if (bluetooth < 0.0f || bluetooth > 1.0f || spdif < 0.0f || spdif > 1.0f || tone < 0.0f || tone > 1.0f) {
        request->send(400, "text/plain", "Gains must be between 0.0 and 1.0");
        return;
    }

    current_config.inputGains.bluetooth = bluetooth;
    current_config.inputGains.spdif = spdif;
    current_config.inputGains.tone = tone;

    scheduleConfigWrite();

    sendToTeensy(CMD_SET_INPUT_GAINS, 
        String(current_config.inputGains.bluetooth, 2),
        String(current_config.inputGains.spdif, 2),
        String(current_config.inputGains.tone, 2));

    request->send(200, "application/json", "{\"status\": \"ok\"}");
}
