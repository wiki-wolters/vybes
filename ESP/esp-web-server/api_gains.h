#ifndef API_GAINS_H
#define API_GAINS_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

void addPresetGainsHandler(AsyncWebServer* server);
void handleGetPresetGains(AsyncWebServerRequest* request);
void handleSetPresetGains(AsyncWebServerRequest* request, JsonVariant& json);
void handlePutSpeakerGain(AsyncWebServerRequest* request);
void handlePutInputGains(AsyncWebServerRequest* request, JsonVariant& json);

#endif