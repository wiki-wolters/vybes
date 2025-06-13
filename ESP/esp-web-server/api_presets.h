#ifndef API_PRESETS_H
#define API_PRESETS_H

#include <ESPAsyncWebServer.h>

void handleGetPresets(AsyncWebServerRequest *request);
void handleGetPreset(AsyncWebServerRequest *request);
void handlePostPresetCreate(AsyncWebServerRequest *request);
void handlePostPresetCopy(AsyncWebServerRequest *request);
void handlePutPresetRename(AsyncWebServerRequest *request);
void handleDeletePreset(AsyncWebServerRequest *request);
void handlePutActivePreset(AsyncWebServerRequest *request);

#endif // API_PRESETS_H
