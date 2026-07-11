#ifndef API_PRESET_CONFIG_H
#define API_PRESET_CONFIG_H

#include <PsychicHttp.h>
#include <ArduinoJson.h>

// EQ management endpoints
esp_err_t handlePutPresetEQPoints(PsychicRequest *request, JsonVariant &json);
esp_err_t handlePutPresetEQPoint(PsychicRequest *request, JsonVariant &json);
esp_err_t handlePutPresetEQEnabled(PsychicRequest *request);

// Crossover endpoints
esp_err_t handlePutPresetCrossover(PsychicRequest *request);
esp_err_t handlePutPresetCrossoverEnabled(PsychicRequest *request);

#endif // API_PRESET_CONFIG_H
