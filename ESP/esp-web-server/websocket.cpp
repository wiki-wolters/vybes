#include "globals.h"
#include "websocket.h"
#include "web_server.h" // Include web_server.h to access the 'server' instance

AsyncWebSocket ws("/live-updates"); // Create the WebSocket instance

void handleWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
            client->text("Connected to Vybes");
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("WebSocket client #%u disconnected\n", client->id());
            break;
        case WS_EVT_DATA:
            // data packet
            if (len > 0) {
                if (type == WS_EVT_DATA) {
                    AwsFrameInfo *info = (AwsFrameInfo*)arg;
                    if (info->final && info->index == 0 && info->len == len) {
                        //the whole message is in a single frame and we got all of it
                        Serial.printf("WebSocket client #%u received: %s\n", client->id(), (char*)data);
                    }
                }
            }
            break;
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

void setupWebSocket() {
    ws.onEvent(handleWebSocketEvent);
    server.addHandler(&ws); // Attach the WebSocket to the AsyncWebServer
    Serial.println("WebSocket server started on /live-updates");
}

void broadcastWebSocket(const char* message) {
    ws.textAll(message);
    Serial.print("WebSocket broadcast: ");
    Serial.println(message);
}
