#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <Arduino.h>

extern WebSocketsServer webSocket;

void setupWebSocket();
void broadcastWebSocket(String message);

#endif
