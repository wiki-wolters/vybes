#ifndef API_FIR_H
#define API_FIR_H

#include <PsychicHttp.h>

esp_err_t handleGetFirFiles(PsychicRequest *request);
esp_err_t handlePutPresetFir(PsychicRequest *request);
esp_err_t handlePutPresetFirEnabled(PsychicRequest *request);

#endif // API_FIR_H
