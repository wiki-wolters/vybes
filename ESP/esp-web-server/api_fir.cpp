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
        // Charged in whole partitions - what the Teensy's static coefficient
        // arena actually spends (see FIR_POOL_CHARGE_QUANTUM)
        uint32_t taps = firFileTaps(file);
        used += (taps + FIR_POOL_CHARGE_QUANTUM - 1) / FIR_POOL_CHARGE_QUANTUM
                * FIR_POOL_CHARGE_QUANTUM;
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

// --- FIR file upload/delete (docs/AUTO_FIR_CONTRACTS.md, Slice A) ---
//
// The browser body is raw file bytes (no multipart wrapper - PsychicHttp's
// upload handler supports this directly, dispatching to its
// non-multipart/"basic" path since the request has no multipart boundary).
// It streams to a small LittleFS temp file chunk by chunk as it arrives -
// the same discipline /restore already uses for its own bounded upload -
// because the wire protocol needs the whole file's CRC32 in firPutBegin
// before a single firPut line goes out, which only a completed body can
// provide; staging through flash (not a heap buffer) is what keeps that
// requirement compatible with "never buffer the whole file in RAM". Once
// the body is complete, handleFirUploadComplete does the actual work: CRC,
// the Teensy UART handshake, and the HTTP response.
#define FIR_UPLOAD_TIMEOUT_MS 5000UL
static const char* FIR_UPLOAD_TMP_PATH = "/fir_upload.tmp";

static File firUploadStagingFile;
static bool firUploadStagingError = false;
// Set when Phase 1's single-flight TryBegin lost the race - Phase 2 must
// not call firUploadRelease() in that case (no guard was ever acquired).
static bool firUploadRejectedBusy = false;
static uint64_t firUploadStagedBytes = 0;

esp_err_t handleFirUploadChunk(PsychicRequest *request, const String& filename,
                               uint64_t index, uint8_t *data, size_t len, bool last) {
    if (index == 0) {
        firUploadStagingError = false;
        firUploadRejectedBusy = false;
        firUploadStagedBytes = 0;

        if (!firUploadTryBegin()) {
            // Another upload or delete is already in flight - let the
            // request drain harmlessly (returning ESP_FAIL here would abort
            // the connection with a generic 500 before Phase 2 could reply
            // 409); Phase 2 sends the real response.
            firUploadRejectedBusy = true;
        } else if (request->contentLength() > FIR_UPLOAD_MAX_SIZE) {
            // Oversized: don't bother staging it, Phase 2 rejects with 400.
            firUploadStagingError = true;
        } else {
            LittleFS.remove(FIR_UPLOAD_TMP_PATH);
            firUploadStagingFile = LittleFS.open(FIR_UPLOAD_TMP_PATH, "w");
            if (!firUploadStagingFile) firUploadStagingError = true;
        }
    }

    if (!firUploadRejectedBusy && !firUploadStagingError && len > 0) {
        if (firUploadStagingFile.write(data, len) != len) {
            firUploadStagingError = true;
        } else {
            firUploadStagedBytes += len;
        }
    }

    if (last && firUploadStagingFile) {
        firUploadStagingFile.close();
    }
    return ESP_OK; // keep draining the socket; Phase 2 reports any failure
}

// Encodes one JSON error object {"error": reason} into a fresh String.
static String firErrorJson(const char* reason) {
    JsonDocument doc;
    doc["error"] = reason;
    String out;
    serializeJson(doc, out);
    return out;
}

esp_err_t handleFirUploadComplete(PsychicRequest *request) {
    if (firUploadRejectedBusy) {
        return request->reply(409, "application/json", firErrorJson("busy").c_str());
    }

    // Phase 1 successfully acquired the single-flight guard - every exit
    // path below must release it exactly once, and the temp file is done
    // with either way.
    struct UploadGuard {
        ~UploadGuard() {
            firUploadRelease();
            LittleFS.remove(FIR_UPLOAD_TMP_PATH);
        }
    } guard;

    if (firUploadStagingError) {
        return request->reply(400, "text/plain", "Upload too large or failed to stage");
    }
    if (!request->hasParam("name")) {
        return request->reply(400, "text/plain", "Missing name parameter");
    }
    String name = request->getParam("name")->value();
    if (name.length() == 0 || !isValidFirFilename(name)) {
        return request->reply(400, "text/plain", "Invalid FIR filename");
    }
    size_t nameLen = name.length();
    bool isBin = nameLen > 4 && strcasecmp(name.c_str() + nameLen - 4, ".bin") == 0;
    bool isWav = nameLen > 4 && strcasecmp(name.c_str() + nameLen - 4, ".wav") == 0;
    bool isTxt = nameLen > 4 && strcasecmp(name.c_str() + nameLen - 4, ".txt") == 0;
    if (!isBin && !isWav && !isTxt) {
        return request->reply(400, "text/plain", "Name must end in .bin, .wav, or .txt");
    }

    uint64_t size = firUploadStagedBytes;
    if (size < FIR_UPLOAD_MIN_SIZE || size > FIR_UPLOAD_MAX_SIZE) {
        return request->reply(400, "text/plain", "Size must be 4-51200 bytes");
    }
    if (isBin && (size % 4) != 0) {
        return request->reply(400, "text/plain", "A .bin size must be a multiple of 4");
    }
    // firPutBegin's line ("firPutBegin <name> <size> <crc32>") must itself
    // fit TEENSY_MSG_MAX - the longest names FIR_FILENAME_LEN otherwise
    // allows can overflow it once the size/crc32 overhead is counted (see
    // teensy_comm.h). Rejecting here (400) beats silently truncating the
    // wire line.
    if (!firUploadNameFits(name.c_str(), (uint32_t)size)) {
        return request->reply(400, "text/plain", "Filename too long for a firPutBegin line");
    }

    // Changing SD contents while the recorder/player holds the card would
    // race its own streaming I/O.
    if (isRecordingActive()) {
        return request->reply(409, "application/json", firErrorJson("recording").c_str());
    }

    // CRC32 over the staged bytes: one sequential pass with a small fixed
    // buffer, so this never holds the whole file in memory either.
    File in = LittleFS.open(FIR_UPLOAD_TMP_PATH, "r");
    if (!in) {
        return request->reply(500, "text/plain", "Failed to read staged upload");
    }
    uint32_t crc = crc32Init();
    {
        uint8_t buf[512];
        int n;
        while ((n = in.read(buf, sizeof(buf))) > 0) {
            crc = crc32Update(crc, buf, (size_t)n);
        }
    }
    char crcHex[9];
    crc32ToHex(crc32Finish(crc), crcHex);

    char errReason[16] = "";
    FirUploadStatus status = firUploadBegin(name.c_str(), (uint32_t)size, crcHex,
                                            FIR_UPLOAD_TIMEOUT_MS, errReason, sizeof(errReason));
    if (status == FIR_UPLOAD_TIMEOUT) {
        in.close();
        firUploadAbort();
        return request->reply(504, "text/plain", "Teensy did not respond");
    }
    if (status == FIR_UPLOAD_ERR) {
        in.close();
        return request->reply(502, "application/json", firErrorJson(errReason).c_str());
    }

    // Stream the staged file to the Teensy as base64 lines - one small
    // fixed buffer per chunk, the same heap discipline as the HTTP side.
    in.seek(0);
    int32_t seq = 0;
    bool timedOut = false;
    for (;;) {
        uint8_t chunk[FIR_PUT_CHUNK_BYTES];
        int r = in.read(chunk, sizeof(chunk));
        if (r <= 0) break;
        char b64[FIR_PUT_B64_MAX + 1];
        base64Encode(chunk, (size_t)r, b64, sizeof(b64));
        status = firUploadPutLine(seq, b64, FIR_UPLOAD_TIMEOUT_MS, errReason, sizeof(errReason));
        if (status != FIR_UPLOAD_OK) {
            timedOut = (status == FIR_UPLOAD_TIMEOUT);
            break;
        }
        seq++;
    }
    in.close();

    if (status != FIR_UPLOAD_OK) {
        if (timedOut) {
            firUploadAbort();
            return request->reply(504, "text/plain", "Teensy did not respond");
        }
        return request->reply(502, "application/json", firErrorJson(errReason).c_str());
    }

    uint32_t finalSize = 0, finalTaps = 0;
    status = firUploadEnd(FIR_UPLOAD_TIMEOUT_MS, &finalSize, &finalTaps, errReason, sizeof(errReason));
    if (status == FIR_UPLOAD_TIMEOUT) {
        firUploadAbort();
        return request->reply(504, "text/plain", "Teensy did not respond");
    }
    if (status == FIR_UPLOAD_ERR) {
        return request->reply(502, "application/json", firErrorJson(errReason).c_str());
    }

    // The upload just changed the SD's file set - invalidate the cache so
    // an immediate GET /fir/files sees it (the standing "first list after a
    // change is stale" trap).
    requestFirFilesRefresh();

    JsonDocument doc;
    doc["name"] = name;
    doc["size"] = finalSize;
    doc["taps"] = finalTaps;
    String body;
    serializeJson(doc, body);
    return request->reply(200, "application/json", body.c_str());
}

esp_err_t handleDeleteFirFile(PsychicRequest *request) {
    if (!request->hasParam("name")) {
        return request->reply(400, "text/plain", "Missing name parameter");
    }
    String name = request->getParam("name")->value();
    if (name.length() == 0 || !isValidFirFilename(name)) {
        return request->reply(400, "text/plain", "Invalid FIR filename");
    }

    if (isRecordingActive()) {
        return request->reply(409, "application/json", firErrorJson("recording").c_str());
    }

    // Refuse to delete a file any preset's output still references (copied
    // out under the config lock so nothing holds a pointer into
    // current_config past its scope).
    char referencingPresets[MAX_PRESETS][PRESET_NAME_MAX_LEN];
    int referencingCount = 0;
    {
        ConfigLock lock;
        for (int p = 0; p < MAX_PRESETS; p++) {
            const Preset& preset = current_config.presets[p];
            if (preset.name[0] == '\0') continue;
            for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
                if (strcmp(preset.outputs[ch].fir, name.c_str()) == 0) {
                    strlcpy(referencingPresets[referencingCount], preset.name, PRESET_NAME_MAX_LEN);
                    referencingCount++;
                    break;
                }
            }
        }
    }
    if (referencingCount > 0) {
        JsonDocument doc;
        doc["error"] = "referenced";
        JsonArray arr = doc.createNestedArray("presets");
        for (int i = 0; i < referencingCount; i++) arr.add(referencingPresets[i]);
        String body;
        serializeJson(doc, body);
        return request->reply(409, "application/json", body.c_str());
    }

    if (!firUploadTryBegin()) {
        return request->reply(409, "application/json", firErrorJson("busy").c_str());
    }
    char errReason[16] = "";
    bool notFound = false;
    FirUploadStatus status = firUploadDelete(name.c_str(), FIR_UPLOAD_TIMEOUT_MS,
                                             errReason, sizeof(errReason), &notFound);
    firUploadRelease();

    if (status == FIR_UPLOAD_TIMEOUT) {
        return request->reply(504, "text/plain", "Teensy did not respond");
    }
    if (status == FIR_UPLOAD_ERR) {
        if (notFound) return request->reply(404, "text/plain", "No such FIR file");
        return request->reply(502, "application/json", firErrorJson(errReason).c_str());
    }

    requestFirFilesRefresh();
    JsonDocument doc;
    doc["name"] = name;
    String body;
    serializeJson(doc, body);
    return request->reply(200, "application/json", body.c_str());
}
