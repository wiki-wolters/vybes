#ifndef API_SYSTEM_H
#define API_SYSTEM_H

#include <PsychicHttp.h>

esp_err_t handleGetStatus(PsychicRequest *request);
esp_err_t handlePutMute(PsychicRequest *request);
esp_err_t handlePutMutePercent(PsychicRequest *request);

#endif // API_SYSTEM_H
