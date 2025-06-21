#ifndef API_DELAY_H
#define API_DELAY_H

#include <ESPAsyncWebServer.h>

void handlePutPresetDelayEnabled(AsyncWebServerRequest *request);
void handlePutPresetDelayNamed(AsyncWebServerRequest *request);

#endif // API_DELAY_H