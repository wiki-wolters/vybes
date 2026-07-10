#ifndef API_SYSTEM_H
#define API_SYSTEM_H

#include <ESPAsyncWebServer.h>

void handleGetStatus(AsyncWebServerRequest *request);
void handlePutMute(AsyncWebServerRequest *request);
void handlePutMutePercent(AsyncWebServerRequest *request);

#endif // API_SYSTEM_H
