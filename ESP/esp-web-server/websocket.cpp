#include "globals.h"
#include "websocket.h"
#include "teensy_comm.h"

// Shared between the HTTP and HTTPS servers - both attach it to
// /live-updates in setupWebServer, and its client list covers both.
PsychicWebSocketHandler ws;

// RTA subscription: the analyzer page sends "rta:keepalive" over the socket
// every couple of seconds while it is open. While those keepalives are
// fresh we keep the Teensy's RTA streaming enabled; when they stop (page
// closed, tab hidden, client gone) we turn it off again. The Teensy also
// times out on its own, so a dropped connection can't leave it streaming.
#define RTA_CLIENT_TIMEOUT_MS 5000
#define RTA_TEENSY_REFRESH_MS 2000
static unsigned long rtaLastClientKeepaliveAt = 0;
static bool rtaActive = false;

void setupWebSocket() {
    ws.onOpen([](PsychicWebSocketClient *client) {
        DebugSerial.printf("WebSocket client #%d connected from %s\n",
                           client->socket(), client->remoteIP().toString().c_str());
        // Clients JSON-parse every message, so the greeting must be JSON
        client->sendMessage("{\"type\":\"hello\",\"message\":\"Connected to Vybes\"}");
    });

    ws.onFrame([](PsychicWebSocketRequest *request, httpd_ws_frame *frame) {
        if (frame->type == HTTPD_WS_TYPE_TEXT && frame->len > 0) {
            // frame->payload is not null-terminated - compare with length
            if (frame->len == 13 && strncmp((const char*)frame->payload, "rta:keepalive", 13) == 0) {
                rtaLastClientKeepaliveAt = millis();
                return ESP_OK;
            }
            DebugSerial.printf("WebSocket received: %.*s\n", (int)frame->len, (const char*)frame->payload);
        }
        return ESP_OK;
    });

    ws.onClose([](PsychicWebSocketClient *client) {
        DebugSerial.printf("WebSocket client #%d disconnected\n", client->socket());
    });

    DebugSerial.println("WebSocket handler ready on /live-updates");
}

void broadcastWebSocket(const char* message) {
    ws.sendAll(message);
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
    ws.sendAll(buf);
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
