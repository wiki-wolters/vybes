#ifndef API_GAINS_H
#define API_GAINS_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PsychicHttp.h>

esp_err_t handleGetPresetGains(PsychicRequest* request);
esp_err_t handleSetPresetGains(PsychicRequest* request, JsonVariant& json);
esp_err_t handlePutSpeakerGain(PsychicRequest* request);
esp_err_t handlePutInputGains(PsychicRequest* request, JsonVariant& json);

#endif
