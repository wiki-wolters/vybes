#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <ESPAsyncWebServer.h>

extern AsyncWebServer server;

void setupWebServer();
void handleBackup(AsyncWebServerRequest *request);
void handleRestore(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final);

#endif // WEB_SERVER_H
