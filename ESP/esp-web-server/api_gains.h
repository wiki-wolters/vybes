#ifndef API_GAINS_H
#define API_GAINS_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PsychicHttp.h>

// /gains/speaker survives for the remote/button path until that is
// reworked onto output gains; the UI no longer calls it.
esp_err_t handlePutSpeakerGain(PsychicRequest* request);
esp_err_t handlePutInputGains(PsychicRequest* request, JsonVariant& json);

#endif
