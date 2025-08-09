#ifndef API_PRESET_CONFIG_H
#define API_PRESET_CONFIG_H

#include <ESPAsyncWebServer.h>

// EQ management endpoints
void handlePostPresetEQ(AsyncWebServerRequest *request);
void handleDeletePresetEQ(AsyncWebServerRequest *request);
void handlePutPresetEQPoints(AsyncWebServerRequest *request, JsonVariant &json);
void handlePutPresetEQEnabled(AsyncWebServerRequest *request);

// Crossover and equal loudness endpoints
void handlePutPresetCrossover(AsyncWebServerRequest *request);
void handlePutPresetCrossoverEnabled(AsyncWebServerRequest *request);

#endif // API_PRESET_CONFIG_H
