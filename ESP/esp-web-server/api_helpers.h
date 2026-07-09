#ifndef API_HELPERS_H
#define API_HELPERS_H

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

int find_preset_by_name(const char* name);
int find_empty_preset_slot();

// Serialize a small JSON document, send it as the HTTP response and
// broadcast it to WebSocket clients. Every broadcast should carry a
// "messageType" field so the UI can dispatch on it.
void sendJsonAndBroadcast(AsyncWebServerRequest* request, const JsonDocument& doc);

#endif // API_HELPERS_H
