#ifndef API_PRESET_CONFIG_H
#define API_PRESET_CONFIG_H

#include <ESPAsyncWebServer.h>

void handlePutPresetDelay(AsyncWebServerRequest *request);
void handlePutPresetDelayNamed(AsyncWebServerRequest *request);
void handlePostPresetEQ(AsyncWebServerRequest *request);
void handleDeletePresetEQ(AsyncWebServerRequest *request);
void handlePutPresetCrossover(AsyncWebServerRequest *request);
void handlePutPresetEqualLoudness(AsyncWebServerRequest *request);
void handlePutPresetEQPoints(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

#endif // API_PRESET_CONFIG_H
