#include <Arduino.h>         // Core Arduino functionality
#include <ArduinoJson.h>     // JSON parsing and generation
#include <string.h>          // For strtok, strlen, strncmp
#include "globals.h"
#include "config.h"
#include "web_server.h"
#include "websocket.h"
#include "teensy_comm.h"
#include "utilities.h"
#include "api_helpers.h"
#include "api_fir.h"

using namespace ArduinoJson;

// The filename travels inside a single space-separated UART line, so it must
// be one clean token that fits the message buffer: "setFir <ch> <name>\n"
// must stay under TEENSY_MSG_MAX or buildMessage truncates it.
#define FIR_FILENAME_MAX FIR_FILENAME_LEN

bool isValidFirFilename(const String& filename) {
    if (filename.length() > FIR_FILENAME_MAX) {
        return false;
    }
    for (size_t i = 0; i < filename.length(); i++) {
        unsigned char c = filename[i];
        if (c <= ' ' || c == 0x7F) { // spaces and control characters
            return false;
        }
    }
    return true;
}

// --- Tap accounting for the shared FIR pool ---
// Tap counts come from the SD listing the Teensy reports:
//   - WAV and TXT files carry an exact count as the line's third token
//     ("name size taps": WAV parsed from the header, TXT tokenized the same
//     way the loader parses it) - used verbatim, so exact-fit pool configs
//     are accepted
//   - otherwise the count is estimated from the file size: .bin files are
//     raw float32 taps (size / 4, exact); text files listed by older
//     firmware without a taps token fall back to size / 12 (a poor fit for
//     rePhase exports, which average ~23 bytes per coefficient - the exact
//     listing count exists precisely because no divisor fits every tool)
//   - files without a known size count as a flat default
// This accounting is what the API enforces and the UI displays; the
// Teensy's own load-time pool check remains the authoritative backstop.
#define FIR_BIN_BYTES_PER_TAP 4
#define FIR_TEXT_BYTES_PER_TAP 12
#define FIR_TAPS_UNKNOWN 2048

uint32_t firFileTaps(const char* file) {
    if (file == nullptr || file[0] == '\0') {
        return 0;
    }
    long taps = getCachedFirFileTaps(file);
    if (taps > 0) {
        return (uint32_t)taps;
    }
    long size = getCachedFirFileSize(file);
    if (size < 0) {
        return FIR_TAPS_UNKNOWN;
    }
    size_t len = strlen(file);
    bool isBin = len > 4 && strcasecmp(file + len - 4, ".bin") == 0;
    long bytesPerTap = isBin ? FIR_BIN_BYTES_PER_TAP : FIR_TEXT_BYTES_PER_TAP;
    return (uint32_t)((size + bytesPerTap - 1) / bytesPerTap);
}

uint32_t firPoolUsed(const Preset& preset, int overrideOutput, const char* overrideFile) {
    uint32_t used = 0;
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        const char* file = (i == overrideOutput) ? overrideFile : preset.outputs[i].fir;
        used += firFileTaps(file);
    }
    return used;
}

// --- Handlers ---

esp_err_t handleGetFirFiles(PsychicRequest *request) {
    // The file list is served from a cache that is refreshed asynchronously
    // over the Teensy link (at boot, when the Teensy reboots, and after each
    // request so the next fetch is fresh).
    requestFirFilesRefresh();

    // strtok modifies its input, so work on a copy of the cache (a stack
    // copy - this handler runs concurrently on both server tasks)
    char listCopy[1024];
    copyCachedFirFiles(listCopy, sizeof(listCopy));

    if (strlen(listCopy) == 0) {
        return request->reply(200, "application/json", "[]");
    }

    // The cache is a newline-separated list of "name size [taps]" (or bare
    // "name") lines; the API returns a plain array of names.
    JsonDocument doc;
    JsonArray files = doc.to<JsonArray>();

    char* line = strtok(listCopy, "\n");
    while (line != NULL) {
        // Skip empty lines and error messages
        if (strlen(line) > 0 && strncmp(line, "ERROR", 5) != 0) {
            // The name is the first token (filenames can't contain spaces)
            char* space = strchr(line, ' ');
            if (space != NULL) {
                *space = '\0';
            }
            files.add(line);
        }
        line = strtok(NULL, "\n");
    }

    // Serialize the response
    String jsonResponse;
    serializeJson(files, jsonResponse);

    // Send the response
    return request->reply(200, "application/json", jsonResponse.c_str());
}

// Fill a "firPool" object: capacity, usage, and any per-output load failures.
// Failures only make sense for the active preset - they describe what the
// Teensy actually has loaded right now, not what a stored preset would load.
void firPoolToJson(const Preset& preset, bool isActive, JsonObject pool) {
    pool["total"] = FIR_TAP_POOL;
    pool["used"] = firPoolUsed(preset);
    firPoolErrorsToJson(isActive, pool);
}

// The web UI replaces its whole firPool object from every broadcast carrying
// one, so each of them has to include the errors or an unrelated edit would
// silently clear the warning.
void firPoolErrorsToJson(bool isActive, JsonObject pool) {
    if (!isActive) return;
    JsonArray errors = pool.createNestedArray("errors");
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        char code[12];
        char file[FIR_FILENAME_LEN + 1];
        if (!getFirLoadError(i, code, sizeof(code), file, sizeof(file))) continue;
        JsonObject entry = errors.createNestedObject();
        entry["output"] = i;
        entry["code"] = code;
        entry["file"] = file;
    }
}

// See the header: the counterpart to broadcastFirLoadError, for the
// transition nothing else reports - a load starting clean.
void broadcastFirPool(const Preset& preset) {
    JsonDocument doc;
    doc["messageType"] = "firPoolChanged";
    doc["presetName"] = preset.name;
    firPoolToJson(preset, true, doc.createNestedObject("firPool"));
    String out;
    serializeJson(doc, out);
    broadcastWebSocket(out.c_str());
}

// GET /preset/fir/pool - tap pool status for a preset
esp_err_t handleGetPresetFirPool(PsychicRequest *request) {
    if (!request->hasParam("preset_name")) {
        return request->reply(400, "text/plain", "Missing preset_name parameter");
    }
    String presetName = request->getParam("preset_name")->value();

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        return request->reply(404, "text/plain", "Preset not found");
    }
    const Preset& preset = current_config.presets[presetIndex];

    JsonDocument doc;
    doc["total"] = FIR_TAP_POOL;
    doc["used"] = firPoolUsed(preset);
    JsonArray outputs = doc.createNestedArray("outputs");
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        JsonObject entry = outputs.createNestedObject();
        entry["output"] = i;
        entry["file"] = preset.outputs[i].fir;
        entry["taps"] = firFileTaps(preset.outputs[i].fir);
    }

    String response;
    serializeJson(doc, response);
    return request->reply(200, "application/json", response.c_str());
}

esp_err_t handlePutPresetFirEnabled(PsychicRequest *request) {
    if (!request->hasParam("preset_name")) {
        return request->reply(400, "text/plain", "Missing preset_name parameter");
    }
    if (!request->hasParam("state")) {
        return request->reply(400, "text/plain", "Missing state parameter");
    }
    String presetName = request->getParam("preset_name")->value();
    String state = request->getParam("state")->value();

    if (state != "on" && state != "off") {
        return request->reply(400, "text/plain", "Invalid state");
    }

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        return request->reply(404, "text/plain", "Preset not found");
    }

    // Toggling FIR on the active preset triggers a FIR load on the Teensy,
    // whose SD reads would glitch a running recording.
    if (presetIndex == current_config.active_preset_index && isRecordingActive()) {
        return request->reply(409, "text/plain", "FIR changes are locked while recording");
    }

    // Update the FIR filter enabled state
    bool enabled = (state == "on");
    {
        ConfigLock lock;
        current_config.presets[presetIndex].firEnabled = enabled;
        scheduleConfigWrite();
    }

    if (presetIndex == current_config.active_preset_index) {
        sendOnOffToTeensy(CMD_SET_FIR_ENABLED, enabled);
        if (enabled) {
            loadFirFilters();
        }
    }

    // Prepare and send response
    JsonDocument doc;
    doc["messageType"] = "firEnabledChanged";
    doc["presetName"] = presetName;
    doc["status"] = "ok";
    doc["FIRFiltersEnabled"] = enabled;
    return sendJsonAndBroadcast(request, doc);
}
