#include "globals.h"
#include "api_outputs.h"
#include "api_helpers.h"
#include "api_fir.h"
#include "config.h"
#include "teensy_comm.h"
#include "websocket.h"
#include <ArduinoJson.h>
#include <stdlib.h>
#include <string.h>

// --- Shared request plumbing ---

struct OutputRequest {
    int presetIndex;
    int outputIndex;
    Preset* preset;
    Output* output;
};

static float clampf(float value, float lo, float hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static double clampd(double value, double lo, double hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

// Parse a strictly numeric query parameter as double. Returns false on a
// missing parameter or trailing garbage ("1.5x").
static bool parseNumberParam(PsychicRequest* request, const char* name, double& out) {
    if (!request->hasParam(name)) return false;
    String value = request->getParam(name)->value();
    if (value.length() == 0) return false;
    char* end = nullptr;
    out = strtod(value.c_str(), &end);
    return end != nullptr && *end == '\0';
}

// Common preset_name + output parameter handling. On failure the reply has
// been sent (stored in result) and false is returned. Check order matches
// the mock: missing/invalid params 400 before the preset lookup 404.
static bool getOutputRequest(PsychicRequest* request, OutputRequest& ctx, esp_err_t& result) {
    if (!request->hasParam("preset_name")) {
        result = request->reply(400, "text/plain", "Missing preset_name parameter");
        return false;
    }
    String presetName = request->getParam("preset_name")->value();

    bool validOutput = false;
    long outputIndex = -1;
    if (request->hasParam("output")) {
        String outputParam = request->getParam("output")->value();
        char* end = nullptr;
        outputIndex = strtol(outputParam.c_str(), &end, 10);
        validOutput = outputParam.length() > 0 && end != nullptr && *end == '\0'
                      && outputIndex >= 0 && outputIndex < NUM_OUTPUTS;
    }
    if (!validOutput) {
        result = request->reply(400, "text/plain", "Output must be an integer 0-7");
        return false;
    }

    int presetIndex = find_preset_by_name(presetName.c_str());
    if (presetIndex == -1) {
        result = request->reply(404, "text/plain", "Preset not found");
        return false;
    }

    ctx.presetIndex = presetIndex;
    ctx.outputIndex = (int)outputIndex;
    ctx.preset = &current_config.presets[presetIndex];
    ctx.output = &ctx.preset->outputs[outputIndex];
    return true;
}

static bool isActivePreset(const OutputRequest& ctx) {
    return ctx.presetIndex == current_config.active_preset_index;
}

// Reply + broadcast the outputChanged shape. The caller pre-populates
// doc["changes"] (and optional extras like firPool); this fills in the
// envelope. flippedTemplate adds the one-time "template":"custom" report.
static esp_err_t replyOutputChanged(PsychicRequest* request, const OutputRequest& ctx,
                                    JsonDocument& doc, bool flippedTemplate = false) {
    doc["messageType"] = "outputChanged";
    doc["presetName"] = ctx.preset->name;
    doc["status"] = "ok";
    doc["output"] = ctx.outputIndex;
    if (flippedTemplate) {
        doc["template"] = "custom";
    }
    return sendJsonAndBroadcast(request, doc);
}

// --- Simple per-output values ---

esp_err_t handlePutOutputLabel(PsychicRequest *request) {
    OutputRequest ctx;
    esp_err_t result;
    if (!getOutputRequest(request, ctx, result)) return result;

    if (!request->hasParam("label")) {
        return request->reply(400, "text/plain", "Missing label parameter");
    }
    String label = request->getParam("label")->value();
    if (label.length() == 0 || label.length() > OUTPUT_LABEL_MAX_LEN) {
        return request->reply(400, "text/plain", "Label must be 1-24 characters");
    }

    {
        ConfigLock lock;
        strlcpy(ctx.output->label, label.c_str(), sizeof(ctx.output->label));
        scheduleConfigWrite();
    }

    JsonDocument doc;
    doc.createNestedObject("changes")["label"] = label;
    return replyOutputChanged(request, ctx, doc);
}

esp_err_t handlePutOutputEnabled(PsychicRequest *request) {
    OutputRequest ctx;
    esp_err_t result;
    if (!getOutputRequest(request, ctx, result)) return result;

    String state = request->hasParam("state") ? request->getParam("state")->value() : "";
    if (state != "on" && state != "off") {
        return request->reply(400, "text/plain", "Invalid state");
    }
    bool enabled = (state == "on");

    bool flipped;
    {
        ConfigLock lock;
        ctx.output->enabled = enabled;
        flipped = flip_template_to_custom(*ctx.preset);
        scheduleConfigWrite();
    }

    if (isActivePreset(ctx)) {
        char ch[8];
        snprintf(ch, sizeof(ch), "%d", ctx.outputIndex);
        sendToTeensy(CMD_SET_OUTPUT_MUTE, ch, (ctx.output->mute || !enabled) ? "1" : "0");
    }

    JsonDocument doc;
    doc.createNestedObject("changes")["enabled"] = enabled;
    return replyOutputChanged(request, ctx, doc, flipped);
}

esp_err_t handlePutOutputSource(PsychicRequest *request, JsonVariant &json) {
    OutputRequest ctx;
    esp_err_t result;
    if (!getOutputRequest(request, ctx, result)) return result;

    JsonObject body = json.as<JsonObject>();
    if (body.isNull() || !body["left"].is<double>() || !body["right"].is<double>()) {
        return request->reply(400, "text/plain", "Expected a JSON body with numeric left and right");
    }
    double left = clampd(body["left"].as<double>(), 0.0, 1.0);
    double right = clampd(body["right"].as<double>(), 0.0, 1.0);

    bool flipped;
    {
        ConfigLock lock;
        ctx.output->sourceLeft = left;
        ctx.output->sourceRight = right;
        flipped = flip_template_to_custom(*ctx.preset);
        scheduleConfigWrite();
    }

    if (isActivePreset(ctx)) {
        char ch[8], l[16], r[16];
        snprintf(ch, sizeof(ch), "%d", ctx.outputIndex);
        snprintf(l, sizeof(l), "%.4f", left);
        snprintf(r, sizeof(r), "%.4f", right);
        sendToTeensy(CMD_SET_OUTPUT_SOURCE, ch, l, r);
    }

    JsonDocument doc;
    JsonObject source = doc.createNestedObject("changes").createNestedObject("source");
    source["left"] = left;
    source["right"] = right;
    return replyOutputChanged(request, ctx, doc, flipped);
}

esp_err_t handlePutOutputGain(PsychicRequest *request) {
    OutputRequest ctx;
    esp_err_t result;
    if (!getOutputRequest(request, ctx, result)) return result;

    double value;
    if (!parseNumberParam(request, "value", value)) {
        return request->reply(400, "text/plain", "Missing or invalid value");
    }
    double gainDb = clampd(value, OUTPUT_GAIN_MIN_DB, OUTPUT_GAIN_MAX_DB);

    {
        ConfigLock lock;
        ctx.output->gainDb = gainDb;
        scheduleConfigWrite();
    }

    if (isActivePreset(ctx)) {
        char ch[8], db[16];
        snprintf(ch, sizeof(ch), "%d", ctx.outputIndex);
        snprintf(db, sizeof(db), "%.2f", gainDb);
        sendToTeensy(CMD_SET_OUTPUT_GAIN, ch, db);
    }

    JsonDocument doc;
    doc.createNestedObject("changes")["gainDb"] = gainDb;
    return replyOutputChanged(request, ctx, doc);
}

esp_err_t handlePutOutputMute(PsychicRequest *request) {
    OutputRequest ctx;
    esp_err_t result;
    if (!getOutputRequest(request, ctx, result)) return result;

    String state = request->hasParam("state") ? request->getParam("state")->value() : "";
    if (state != "on" && state != "off") {
        return request->reply(400, "text/plain", "Invalid state");
    }
    bool mute = (state == "on");

    {
        ConfigLock lock;
        ctx.output->mute = mute;
        scheduleConfigWrite();
    }

    if (isActivePreset(ctx)) {
        char ch[8];
        snprintf(ch, sizeof(ch), "%d", ctx.outputIndex);
        sendToTeensy(CMD_SET_OUTPUT_MUTE, ch, (mute || !ctx.output->enabled) ? "1" : "0");
    }

    JsonDocument doc;
    doc.createNestedObject("changes")["mute"] = mute;
    return replyOutputChanged(request, ctx, doc);
}

esp_err_t handlePutOutputInvert(PsychicRequest *request) {
    OutputRequest ctx;
    esp_err_t result;
    if (!getOutputRequest(request, ctx, result)) return result;

    String state = request->hasParam("state") ? request->getParam("state")->value() : "";
    if (state != "on" && state != "off") {
        return request->reply(400, "text/plain", "Invalid state");
    }
    bool invert = (state == "on");

    {
        ConfigLock lock;
        ctx.output->invert = invert;
        scheduleConfigWrite();
    }

    if (isActivePreset(ctx)) {
        char ch[8];
        snprintf(ch, sizeof(ch), "%d", ctx.outputIndex);
        sendToTeensy(CMD_SET_OUTPUT_INVERT, ch, invert ? "1" : "0");
    }

    JsonDocument doc;
    doc.createNestedObject("changes")["invert"] = invert;
    return replyOutputChanged(request, ctx, doc);
}

esp_err_t handlePutOutputDelay(PsychicRequest *request) {
    OutputRequest ctx;
    esp_err_t result;
    if (!getOutputRequest(request, ctx, result)) return result;

    double delayUs;
    if (!parseNumberParam(request, "value", delayUs) || delayUs < 0 || delayUs > MAX_DELAY_US) {
        return request->reply(400, "text/plain", "Delay must be between 0 and 20000 microseconds");
    }

    {
        ConfigLock lock;
        ctx.output->delayUs = delayUs;
        scheduleConfigWrite();
    }

    if (isActivePreset(ctx)) {
        char ch[8], us[16];
        snprintf(ch, sizeof(ch), "%d", ctx.outputIndex);
        snprintf(us, sizeof(us), "%d", (int)delayUs);
        sendToTeensy(CMD_SET_OUTPUT_DELAY, ch, us);
    }

    JsonDocument doc;
    doc.createNestedObject("changes")["delayUs"] = delayUs;
    return replyOutputChanged(request, ctx, doc);
}

// --- HP/LP filter sections ---
// Body: {mode:'off'} | {mode:'xover', xover:id} | {mode:'manual', freq, type}.
// hpFloor is enforced on every edit (including switching the HP off).

esp_err_t handlePutOutputFilter(PsychicRequest *request, JsonVariant &json) {
    OutputRequest ctx;
    esp_err_t result;
    if (!getOutputRequest(request, ctx, result)) return result;

    String which = request->hasParam("which") ? request->getParam("which")->value() : "";
    if (which != "hp" && which != "lp") {
        return request->reply(400, "text/plain", "Parameter 'which' must be 'hp' or 'lp'");
    }

    JsonObject body = json.as<JsonObject>();
    if (body.isNull()) {
        return request->reply(400, "text/plain", "Expected a JSON filter object");
    }

    FilterSection& section = (which == "hp") ? ctx.output->hp : ctx.output->lp;
    FilterSection updated = section; // keeps a previous xover ref across 'off'

    const char* mode = body["mode"] | "";
    if (strcmp(mode, "off") == 0) {
        updated.mode = FilterMode::Off;
    } else if (strcmp(mode, "xover") == 0) {
        const char* xover = body["xover"] | "";
        if (find_crossover_by_id(*ctx.preset, xover) == -1) {
            return request->reply(400, "text/plain", "Unknown crossover point");
        }
        updated.mode = FilterMode::Xover;
        strlcpy(updated.xover, xover, sizeof(updated.xover));
    } else if (strcmp(mode, "manual") == 0) {
        double freq = body["freq"] | -1.0;
        const char* type = body["type"] | "LR4";
        if (!(freq >= 20 && freq <= 20000)) {
            return request->reply(400, "text/plain", "Manual filter frequency must be between 20 and 20000 Hz");
        }
        if (strcmp(type, "LR2") != 0 && strcmp(type, "LR4") != 0 && strcmp(type, "BW2") != 0) {
            return request->reply(400, "text/plain", "Filter type must be one of LR2, LR4, BW2");
        }
        updated.mode = FilterMode::Manual;
        updated.freq = freq;
        strlcpy(updated.type, type, sizeof(updated.type));
        updated.xover[0] = '\0'; // a manual section drops any kept reference
    } else {
        return request->reply(400, "text/plain", "Filter mode must be 'off', 'xover' or 'manual'");
    }

    bool flipped;
    {
        ConfigLock lock;
        FilterSection previous = section;
        section = updated;
        int violation = hp_floor_violation(*ctx.preset);
        if (violation >= 0) {
            section = previous;
            const Output& out = ctx.preset->outputs[violation];
            char message[128];
            snprintf(message, sizeof(message), "Output %d (%s) requires a high-pass at or above %u Hz",
                     violation + 1, out.label, out.hpFloor);
            return request->reply(409, "text/plain", message);
        }
        flipped = flip_template_to_custom(*ctx.preset);
        scheduleConfigWrite();
    }

    if (isActivePreset(ctx)) {
        sendOutputFiltersToTeensy(ctx.outputIndex, *ctx.preset);
    }

    JsonDocument doc;
    filter_to_json(section, doc.createNestedObject("changes").createNestedObject(which));
    return replyOutputChanged(request, ctx, doc, flipped);
}

// --- Output PEQ ---

esp_err_t handlePutOutputEq(PsychicRequest *request, JsonVariant &json) {
    OutputRequest ctx;
    esp_err_t result;
    if (!getOutputRequest(request, ctx, result)) return result;

    JsonArray pointsArray = json.as<JsonArray>();
    if (pointsArray.isNull()) {
        return request->reply(400, "text/plain", "Expected a JSON array of PEQ points");
    }
    if ((int)pointsArray.size() > MAX_OUTPUT_PEQ) {
        return request->reply(400, "text/plain", "Too many PEQ points");
    }

    // Points are stored sequentially: array order defines the band index.
    int prev_num_points = ctx.output->num_peq;
    int count = 0;
    bool changed[MAX_OUTPUT_PEQ];

    {
        ConfigLock lock;
        for (JsonObject point : pointsArray) {
            float new_freq = clampf(point["freq"] | 1000.0f, 20.0f, 20000.0f);
            float new_gain = clampf(point["gain"] | 0.0f, -15.0f, 15.0f);
            float new_q    = clampf(point["q"] | 1.0f, 0.1f, 10.0f);

            PEQPoint& stored = ctx.output->peq[count];
            changed[count] = (count >= prev_num_points) ||
                             (new_freq != stored.freq) ||
                             (new_gain != stored.gain) ||
                             (new_q != stored.q);
            stored.freq = new_freq;
            stored.gain = new_gain;
            stored.q = new_q;
            count++;
        }
        ctx.output->num_peq = count;
        scheduleConfigWrite();
    }

    if (isActivePreset(ctx)) {
        // Queue only the points that actually changed...
        for (int i = 0; i < count; i++) {
            if (changed[i]) {
                sendOutputEqPointToTeensy(ctx.outputIndex, i, ctx.output->peq[i]);
            }
        }
        // ...and disable every band beyond the active points
        char ch[8], from[8];
        snprintf(ch, sizeof(ch), "%d", ctx.outputIndex);
        snprintf(from, sizeof(from), "%d", count);
        sendToTeensy(CMD_RESET_OUTPUT_EQ, ch, from);
    }

    JsonDocument doc;
    doc["messageType"] = "outputEqChanged";
    doc["presetName"] = ctx.preset->name;
    doc["status"] = "ok";
    doc["output"] = ctx.outputIndex;
    doc["numPoints"] = count;
    String buffer;
    serializeJson(doc, buffer);
    if (buffer.length() > 0) {
        broadcastWebSocket(buffer.c_str());
    }

    return request->reply(204);
}

// Single output PEQ point (hot path while dragging): update in place or
// append directly after the last point; a gap-leaving id is rejected.
// Replies 204 without broadcasting.
esp_err_t handlePutOutputEqPoint(PsychicRequest *request, JsonVariant &json) {
    OutputRequest ctx;
    esp_err_t result;
    if (!getOutputRequest(request, ctx, result)) return result;

    JsonObject point = json.as<JsonObject>();
    if (point.isNull()) {
        return request->reply(400, "text/plain", "Expected a JSON PEQ point object");
    }

    int id = point["id"] | -1;
    if (id < 0 || id >= MAX_OUTPUT_PEQ) {
        return request->reply(400, "text/plain", "PEQ point ID out of bounds");
    }
    if (id > ctx.output->num_peq) {
        return request->reply(400, "text/plain", "PEQ point ID would leave a gap");
    }

    {
        ConfigLock lock;
        PEQPoint& stored = ctx.output->peq[id];
        stored.freq = clampf(point["freq"] | 1000.0f, 20.0f, 20000.0f);
        stored.gain = clampf(point["gain"] | 0.0f, -15.0f, 15.0f);
        stored.q    = clampf(point["q"] | 1.0f, 0.1f, 10.0f);
        if (id >= ctx.output->num_peq) {
            ctx.output->num_peq = id + 1;
        }
        scheduleConfigWrite();
    }

    if (isActivePreset(ctx)) {
        sendOutputEqPointToTeensy(ctx.outputIndex, id, ctx.output->peq[id]);
    }

    return request->reply(204);
}

// Non-destructive per-output PEQ bypass: the stored points stay, only the
// processing toggles. "off" is honestly raw - no level compensation (and the
// Teensy's shared headroom pad releases whatever this output's boosts were
// costing), so an A/B against it is louder-vs-quieter as well as
// corrected-vs-raw.
esp_err_t handlePutOutputEqEnabled(PsychicRequest *request) {
    OutputRequest ctx;
    esp_err_t result;
    if (!getOutputRequest(request, ctx, result)) return result;

    String state = request->hasParam("state") ? request->getParam("state")->value() : "";
    if (state != "on" && state != "off") {
        return request->reply(400, "text/plain", "Invalid state");
    }
    bool enabled = (state == "on");

    {
        ConfigLock lock;
        ctx.output->eqEnabled = enabled;
        scheduleConfigWrite();
    }

    if (isActivePreset(ctx)) {
        char ch[8];
        snprintf(ch, sizeof(ch), "%d", ctx.outputIndex);
        sendToTeensy(CMD_SET_OUTPUT_EQ_ENABLED, ch, enabled ? "1" : "0");
    }

    JsonDocument doc;
    doc.createNestedObject("changes")["eqEnabled"] = enabled;
    return replyOutputChanged(request, ctx, doc);
}

// --- Per-output FIR file ---
// The shared tap pool is enforced here: a load that would exceed it is
// rejected with a 409 carrying used/total (the UI surfaces them).

esp_err_t handlePutOutputFir(PsychicRequest *request) {
    OutputRequest ctx;
    esp_err_t result;
    if (!getOutputRequest(request, ctx, result)) return result;

    // Changing the active preset's FIR files triggers a FIR load on the
    // Teensy, whose SD reads would glitch a running recording.
    if (isActivePreset(ctx) && isRecordingActive()) {
        return request->reply(409, "text/plain", "FIR changes are locked while recording");
    }

    if (!request->hasParam("file")) {
        return request->reply(400, "text/plain", "Missing file parameter");
    }
    String file = request->getParam("file")->value();

    // An empty filename clears the filter; anything else must survive the
    // UART line protocol intact
    if (!isValidFirFilename(file)) {
        return request->reply(400, "text/plain", "Invalid FIR filename: too long, or contains spaces/control characters");
    }

    // Pool check with this output's file swapped for the candidate
    uint32_t used = firPoolUsed(*ctx.preset, ctx.outputIndex, file.c_str());
    if (used > FIR_TAP_POOL) {
        JsonDocument err;
        char message[80];
        snprintf(message, sizeof(message), "FIR tap pool exceeded: %lu of %d taps",
                 (unsigned long)used, FIR_TAP_POOL);
        err["error"] = message;
        err["used"] = used;
        err["total"] = FIR_TAP_POOL;
        String buffer;
        serializeJson(err, buffer);
        return request->reply(409, "application/json", buffer.c_str());
    }

    {
        ConfigLock lock;
        strlcpy(ctx.output->fir, file.c_str(), sizeof(ctx.output->fir));
        scheduleConfigWrite();
    }

    if (isActivePreset(ctx)) {
        char ch[8];
        snprintf(ch, sizeof(ch), "%d", ctx.outputIndex);
        // Bare "setFir <ch>" clears the filter
        sendToTeensy(CMD_SET_FIR, ch, file.length() > 0 ? file.c_str() : nullptr);
        loadFirFilters();
    }

    JsonDocument doc;
    doc.createNestedObject("changes")["fir"] = file;
    JsonObject pool = doc.createNestedObject("firPool");
    pool["total"] = FIR_TAP_POOL;
    pool["used"] = used;
    firPoolErrorsToJson(ctx.preset == &current_config.presets[current_config.active_preset_index],
                        pool);
    return replyOutputChanged(request, ctx, doc);
}
