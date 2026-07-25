#ifndef API_FIR_H
#define API_FIR_H

#include <PsychicHttp.h>
#include "config.h"

esp_err_t handleGetFirFiles(PsychicRequest *request);
esp_err_t handleGetPresetFirPool(PsychicRequest *request);
esp_err_t handlePutPresetFirEnabled(PsychicRequest *request);

// True when the name survives the UART line protocol intact (fits the
// message buffer, no spaces/control characters). Empty = clear = valid.
bool isValidFirFilename(const String& filename);

// Tap count charged against the shared pool for one file ("" = 0). Derived
// from the Teensy-reported file size; unknown files use a default estimate.
uint32_t firFileTaps(const char* file);

// Total taps a preset's FIR assignments would consume. When overrideOutput
// is >= 0 that output's file is replaced by overrideFile for the count
// (used to price a candidate load before accepting it).
uint32_t firPoolUsed(const Preset& preset, int overrideOutput = -1, const char* overrideFile = nullptr);

#endif // API_FIR_H
