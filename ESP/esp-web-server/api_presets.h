#ifndef API_PRESETS_H
#define API_PRESETS_H

#include <PsychicHttp.h>

esp_err_t handleGetPresets(PsychicRequest *request);
esp_err_t handleGetPreset(PsychicRequest *request);
esp_err_t handlePostPresetCreate(PsychicRequest *request);
esp_err_t handlePostPresetCopy(PsychicRequest *request);
esp_err_t handlePutPresetRename(PsychicRequest *request);
esp_err_t handleDeletePreset(PsychicRequest *request);
esp_err_t handlePutActivePreset(PsychicRequest *request);
esp_err_t handlePutPresetDelayEnabled(PsychicRequest *request);
esp_err_t handlePutPresetDelayNamed(PsychicRequest *request);

#endif // API_PRESETS_H
