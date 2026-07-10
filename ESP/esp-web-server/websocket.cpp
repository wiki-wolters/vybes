#include "globals.h"
#include "websocket.h"
#include "web_server.h" // Include web_server.h to access the 'server' instance
#include "teensy_comm.h"

AsyncWebSocket ws("/live-updates"); // Create the WebSocket instance

// RTA subscription: the analyzer page sends "rta:keepalive" over the socket
// every couple of seconds while it is open. While those keepalives are
// fresh we keep the Teensy's RTA streaming enabled; when they stop (page
// closed, tab hidden, client gone) we turn it off again. The Teensy also
// times out on its own, so a dropped connection can't leave it streaming.
#define RTA_CLIENT_TIMEOUT_MS 5000
#define RTA_TEENSY_REFRESH_MS 2000
static unsigned long rtaLastClientKeepaliveAt = 0;
static bool rtaActive = false;

void handleWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            DebugSerial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
            // Clients JSON-parse every message, so the greeting must be JSON
            client->text("{\"type\":\"hello\",\"message\":\"Connected to Vybes\"}");
            break;
        case WS_EVT_DISCONNECT:
            DebugSerial.printf("WebSocket client #%u disconnected\n", client->id());
            break;
        case WS_EVT_DATA:
            // data packet
            if (len > 0) {
                AwsFrameInfo *info = (AwsFrameInfo*)arg;
                if (info->final && info->index == 0 && info->len == len) {
                    // The whole message is in a single frame and we got all of it.
                    // Note: 'data' is not null-terminated - compare with length.
                    if (len == 13 && strncmp((const char*)data, "rta:keepalive", 13) == 0) {
                        rtaLastClientKeepaliveAt = millis();
                        break;
                    }
                    DebugSerial.printf("WebSocket client #%u received: %.*s\n", client->id(), (int)len, (const char*)data);
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
    DebugSerial.println("WebSocket server started on /live-updates");
}

void broadcastWebSocket(const char* message) {
    ws.textAll(message);
    DebugSerial.print("WebSocket broadcast: ");
    DebugSerial.println(message);
}

// Forward one RTA frame (the hex payload after "RTA ") to all clients.
// Called from teensyCommLoop at ~10Hz, so no debug logging here.
void broadcastRtaFrame(const char* hexData) {
    if (ws.count() == 0) return;
    size_t len = strlen(hexData);
    if (len == 0 || len > 62) return; // 31 bands * 2 hex chars
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"type\":\"rta\",\"d\":\"%s\"}", hexData);
    ws.textAll(buf);
}

// Relay RTA interest to the Teensy: refresh its keepalive while a web
// client wants frames, send a single stop when interest lapses.
void websocketLoop() {
    unsigned long now = millis();
    bool wantRta = rtaLastClientKeepaliveAt != 0 &&
                   now - rtaLastClientKeepaliveAt < RTA_CLIENT_TIMEOUT_MS &&
                   ws.count() > 0;
    static unsigned long lastTeensyRefreshAt = 0;
    if (wantRta) {
        rtaActive = true;
        if (now - lastTeensyRefreshAt >= RTA_TEENSY_REFRESH_MS) {
            lastTeensyRefreshAt = now;
            sendToTeensy(CMD_SET_RTA, "1");
        }
    } else if (rtaActive) {
        rtaActive = false;
        sendToTeensy(CMD_SET_RTA, "0");
    }
}
