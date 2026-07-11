#ifndef API_SIGNAL_GENERATOR_H
#define API_SIGNAL_GENERATOR_H

#include <PsychicHttp.h>

esp_err_t handlePutTone(PsychicRequest *request);
esp_err_t handlePutToneStop(PsychicRequest *request);
esp_err_t handlePutNoise(PsychicRequest *request);

#endif // API_SIGNAL_GENERATOR_H
