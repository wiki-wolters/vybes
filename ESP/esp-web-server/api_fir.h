#ifndef API_FIR_H
#define API_FIR_H

#include <ESPAsyncWebServer.h>

void handleGetFirFiles(AsyncWebServerRequest *request);
void handlePutPresetFir(AsyncWebServerRequest *request);
void handlePutPresetFirEnabled(AsyncWebServerRequest *request);

#endif // API_FIR_H
