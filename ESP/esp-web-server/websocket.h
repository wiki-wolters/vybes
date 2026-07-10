#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <ESPAsyncWebServer.h>

extern AsyncWebSocket ws;

void setupWebSocket();
void broadcastWebSocket(const char* message);

// Forward one Teensy RTA frame (hex payload) to all websocket clients
void broadcastRtaFrame(const char* hexData);

// Tracks client interest in RTA frames and relays it to the Teensy.
// Call from loop().
void websocketLoop();

#endif
