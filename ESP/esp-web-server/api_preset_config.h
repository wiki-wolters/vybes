#ifndef API_PRESET_CONFIG_H
#define API_PRESET_CONFIG_H

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// EQ management endpoints
void handlePutPresetEQPoints(AsyncWebServerRequest *request, JsonVariant &json);
void handlePutPresetEQPoint(AsyncWebServerRequest *request, JsonVariant &json);
void handlePutPresetEQEnabled(AsyncWebServerRequest *request);

// Crossover endpoints
void handlePutPresetCrossover(AsyncWebServerRequest *request);
void handlePutPresetCrossoverEnabled(AsyncWebServerRequest *request);

#endif // API_PRESET_CONFIG_H
