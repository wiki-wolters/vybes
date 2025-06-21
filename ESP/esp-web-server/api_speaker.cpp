#include "globals.h"
#include "config.h"
#include "web_server.h"
#include "websocket.h"
#include "teensy_comm.h"
#include "utilities.h"

void handlePutSpeakerGain(AsyncWebServerRequest *request) {
    String speaker = "";
    String path = request->url();
    
    // Extract speaker name from URL
    if (path.startsWith("/speaker/left/" || 
        path.startsWith("/speaker/right/" || 
        path.startsWith("/speaker/sub/")) {
        speaker = path.substring(9, path.indexOf("/", 9));
    } else {
        request->send(400, "text/plain", "Invalid speaker");
        return;
    }
    
    // Get gain value from URL parameter
    float gain = request->pathArg(0).toFloat();
    if (gain < 0.0f || gain > 2.0f) {
        request->send(400, "text/plain", "Gain must be between 0.0 and 2.0");
        return;
    }

    // Update the appropriate gain value in the config
    if (speaker == "left") {
        current_config.leftGain = gain;
    } else if (speaker == "right") {
        current_config.rightGain = gain;
    } else if (speaker == "sub") {
        current_config.subGain = gain;
    }
    
    // Save the updated configuration
    scheduleConfigWrite();
    
    // Send command to Teensy
    String command = speaker + "_gain " + String(gain, 2);
    sendToTeensy(command.c_str(), "");
    
    // Prepare and send response
    DynamicJsonDocument doc(256);
    doc[speaker + "Gain"] = gain;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
    
    // Broadcast the update to all WebSocket clients
    broadcastWebSocket(response);
}
