#ifndef API_OUTPUTS_H
#define API_OUTPUTS_H

#include <PsychicHttp.h>
#include <ArduinoJson.h>

// V1 per-output-channel endpoints: PUT /preset/output/… with
// preset_name= and output=0..7 (docs/CHANNEL_ARCHITECTURE.md).

esp_err_t handlePutOutputLabel(PsychicRequest *request);
esp_err_t handlePutOutputEnabled(PsychicRequest *request);
esp_err_t handlePutOutputSource(PsychicRequest *request, JsonVariant &json);
esp_err_t handlePutOutputGain(PsychicRequest *request);
esp_err_t handlePutOutputMute(PsychicRequest *request);
esp_err_t handlePutOutputInvert(PsychicRequest *request);
esp_err_t handlePutOutputDelay(PsychicRequest *request);
esp_err_t handlePutOutputFilter(PsychicRequest *request, JsonVariant &json);
esp_err_t handlePutOutputEq(PsychicRequest *request, JsonVariant &json);
esp_err_t handlePutOutputEqPoint(PsychicRequest *request, JsonVariant &json);
esp_err_t handlePutOutputFir(PsychicRequest *request);

#endif // API_OUTPUTS_H
