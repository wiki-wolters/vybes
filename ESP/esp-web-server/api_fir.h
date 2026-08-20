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

// Serialize total/used plus per-output FIR load failures (active preset only).
void firPoolToJson(const Preset& preset, bool isActive, JsonObject pool);

// Just the "errors" array, for callers that compute "used" themselves.
void firPoolErrorsToJson(bool isActive, JsonObject pool);

// Push the active preset's whole tap-pool state - capacity, usage and the
// current error list - to every websocket client. Failures are broadcast as
// they happen, but nothing ever announced their absence: clients merge
// firLoadError messages and had no way to learn a filter recovered, so the
// UI's list only grew, and could show failures from separate loads that
// never coexisted. Sent when a load clears the slate, so clients reset
// before that load's own FIRERR lines arrive.
void broadcastFirPool(const Preset& preset);

#endif // API_FIR_H
