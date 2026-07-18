#include "globals.h"
#include "websocket.h"
#include "web_server.h"
#include "teensy_comm.h"
#include <atomic>

// One handler per listener. esp-idf's httpd runs everything for a server -
// URI handlers (which add websocket clients), session close (which removes
// them) and httpd_queue_work jobs - on that server's single task. Giving
// each listener its own handler and marshaling every broadcast onto the
// owning server's task with httpd_queue_work means a handler's client list
// is only ever touched from one task, so it can never be mutated while a
// broadcast iterates it. (A single handler shared by both listeners - the
// old setup - is unfixable with a mutex on our side: the library mutates
// the list internally on connect/disconnect without taking one.)
PsychicWebSocketHandler wsHttp;
#ifdef CONFIG_IDF_TARGET_ESP32S3
PsychicWebSocketHandler wsHttps;
#endif

// Per-listener client counts, maintained from the handlers' open/close
// callbacks (each runs on its listener's httpd task) and read from the loop
// task to skip work when nobody is connected.
static std::atomic<int> wsHttpClients{0};
static std::atomic<int> wsHttpsClients{0};

static int totalClients() {
    return wsHttpClients.load() + wsHttpsClients.load();
}

// RTA subscription: the analyzer page sends "rta:keepalive" over the socket
// every couple of seconds while it is open. While those keepalives are
// fresh we keep the Teensy's RTA streaming enabled; when they stop (page
// closed, tab hidden, client gone) we turn it off again. The Teensy also
// times out on its own, so a dropped connection can't leave it streaming.
#define RTA_CLIENT_TIMEOUT_MS 5000
#define RTA_TEENSY_REFRESH_MS 2000
static unsigned long rtaLastClientKeepaliveAt = 0;
static bool rtaActive = false;

static void setupHandler(PsychicWebSocketHandler &handler, std::atomic<int> &clientCount) {
    handler.onOpen([&clientCount](PsychicWebSocketClient *client) {
        clientCount.fetch_add(1);
        DebugSerial.printf("WebSocket client #%d connected from %s\n",
                           client->socket(), client->remoteIP().toString().c_str());
        // Clients JSON-parse every message, so the greeting must be JSON
        client->sendMessage("{\"type\":\"hello\",\"message\":\"Connected to Vybes\"}");
    });

    handler.onFrame([](PsychicWebSocketRequest *request, httpd_ws_frame *frame) {
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

    handler.onClose([&clientCount](PsychicWebSocketClient *client) {
        clientCount.fetch_sub(1);
        DebugSerial.printf("WebSocket client #%d disconnected\n", client->socket());
    });
}

void setupWebSocket() {
    setupHandler(wsHttp, wsHttpClients);
#ifdef CONFIG_IDF_TARGET_ESP32S3
    setupHandler(wsHttps, wsHttpsClients);
#endif
    DebugSerial.println("WebSocket handlers ready on /live-updates");
}

// A broadcast queued for one listener's httpd task (message copied inline).
struct WsBroadcast {
    PsychicWebSocketHandler *handler;
    char msg[1]; // over-allocated to hold the whole message
};

static void wsBroadcastWork(void *arg) {
    WsBroadcast *b = (WsBroadcast *)arg;
    b->handler->sendAll(b->msg);
    free(b);
}

static void queueBroadcast(PsychicHttpServer &server, PsychicWebSocketHandler &handler,
                           std::atomic<int> &clientCount, const char *message) {
    if (server.server == NULL || clientCount.load() == 0) {
        return; // listener not started, or nobody to tell
    }
    size_t len = strlen(message);
    WsBroadcast *b = (WsBroadcast *)malloc(sizeof(WsBroadcast) + len);
    if (b == NULL) {
        return;
    }
    b->handler = &handler;
    memcpy(b->msg, message, len + 1);
    if (httpd_queue_work(server.server, wsBroadcastWork, b) != ESP_OK) {
        free(b);
    }
}

static void broadcastToAllListeners(const char *message) {
    queueBroadcast(server, wsHttp, wsHttpClients, message);
#ifdef CONFIG_IDF_TARGET_ESP32S3
    queueBroadcast(serverHttps, wsHttps, wsHttpsClients, message);
#endif
}

void broadcastWebSocket(const char* message) {
    broadcastToAllListeners(message);
    DebugSerial.print("WebSocket broadcast: ");
    DebugSerial.println(message);
}

// Forward one RTA frame (the hex payload after "RTA ") to all clients.
// Called from teensyCommLoop at ~10Hz, so no debug logging here.
void broadcastRtaFrame(const char* hexData) {
    if (totalClients() == 0) return;
    size_t len = strlen(hexData);
    if (len == 0 || len > 62) return; // 31 bands * 2 hex chars
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"type\":\"rta\",\"d\":\"%s\"}", hexData);
    broadcastToAllListeners(buf);
}

// Relay RTA interest to the Teensy: refresh its keepalive while a web
// client wants frames, send a single stop when interest lapses.
void websocketLoop() {
    unsigned long now = millis();
    bool wantRta = rtaLastClientKeepaliveAt != 0 &&
                   now - rtaLastClientKeepaliveAt < RTA_CLIENT_TIMEOUT_MS &&
                   totalClients() > 0;
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
