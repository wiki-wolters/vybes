#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <ESPAsyncWebServer.h>

extern AsyncWebSocket ws;

void setupWebSocket();
void broadcastWebSocket(String message);

#endif
