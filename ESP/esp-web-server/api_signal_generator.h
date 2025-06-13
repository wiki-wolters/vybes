#ifndef API_SIGNAL_GENERATOR_H
#define API_SIGNAL_GENERATOR_H

#include <ESPAsyncWebServer.h>

void handlePutTone(AsyncWebServerRequest *request);
void handlePutToneStop(AsyncWebServerRequest *request);
void handlePutNoise(AsyncWebServerRequest *request);
void handlePutPulse(AsyncWebServerRequest *request);

#endif // API_SIGNAL_GENERATOR_H
