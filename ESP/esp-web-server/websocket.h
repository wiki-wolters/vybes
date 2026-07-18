#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <PsychicHttp.h>

// One websocket handler per listener: a handler's client list is only ever
// touched from its own server's httpd task (connects, disconnects, and
// broadcasts marshaled over via httpd_queue_work), which is what makes
// broadcasting from the loop task safe.
extern PsychicWebSocketHandler wsHttp;
#ifdef CONFIG_IDF_TARGET_ESP32S3
extern PsychicWebSocketHandler wsHttps;
#endif

void setupWebSocket();

// Queue a message to every connected client on every listener. Safe to call
// from any task; the send itself runs on each listener's own httpd task.
void broadcastWebSocket(const char* message);

// Forward one Teensy RTA frame (hex payload) to all websocket clients
void broadcastRtaFrame(const char* hexData);

// Tracks client interest in RTA frames and relays it to the Teensy.
// Call from loop().
void websocketLoop();

#endif
