#include "globals.h"
#include "api_probe.h"
#include "config.h"
#include "teensy_comm.h"
#include "teensy_protocol.h"
#include <ArduinoJson.h>

esp_err_t handlePutProbeDelayStart(PsychicRequest *request) {
    int level = 50;
    if (request->hasParam("level")) {
        String levelParam = request->getParam("level")->value();
        char* end = nullptr;
        long parsed = strtol(levelParam.c_str(), &end, 10);
        if (levelParam.length() == 0 || end == nullptr || *end != '\0'
            || parsed < 0 || parsed > 100) {
            return request->reply(400, "text/plain", "Level must be an integer 0-100");
        }
        level = (int)parsed;
    }

    // The probe covers the active preset's enabled outputs: ascending, then
    // the same list reversed (the UI averages both passes per output to
    // cancel phone-clock drift). The Teensy derives the same order from the
    // mask, so the mask alone is the wire contract.
    int mask = 0;
    int forward[NUM_OUTPUTS];
    int count = 0;
    {
        ConfigLock lock;
        const Preset& preset = current_config.presets[current_config.active_preset_index];
        for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
            if (preset.outputs[ch].enabled) {
                mask |= 1 << ch;
                forward[count++] = ch;
            }
        }
    }
    if (count == 0) {
        return request->reply(400, "text/plain", "Active preset has no enabled outputs");
    }

    char maskStr[8], levelStr[8];
    snprintf(maskStr, sizeof(maskStr), "%d", mask);
    snprintf(levelStr, sizeof(levelStr), "%d", level);
    sendToTeensy(CMD_START_DELAY_PROBE, maskStr, levelStr);

    // The schedule the UI records and correlates against. Sample counts are
    // on the Teensy's 44.1kHz clock; chirp k starts at sample
    // preRollSamples + k * spacingSamples.
    JsonDocument doc;
    doc["status"] = "ok";
    doc["sampleRate"] = PROBE_SAMPLE_RATE;
    doc["preRollSamples"] = PROBE_PRE_ROLL_SAMPLES;
    doc["spacingSamples"] = PROBE_SPACING_SAMPLES;
    doc["chirpSamples"] = PROBE_CHIRP_SAMPLES;
    doc["tailSamples"] = PROBE_TAIL_SAMPLES;
    doc["fadeSamples"] = PROBE_FADE_SAMPLES;
    doc["f0"] = PROBE_F0_HZ;
    doc["f1"] = PROBE_F1_HZ;
    doc["level"] = level;
    JsonArray order = doc.createNestedArray("order");
    for (int i = 0; i < count; i++) order.add(forward[i]);
    for (int i = count - 1; i >= 0; i--) order.add(forward[i]);

    char responseBuffer[512];
    size_t len = serializeJson(doc, responseBuffer, sizeof(responseBuffer));
    if (len == 0 || len >= sizeof(responseBuffer)) {
        return request->reply(500, "application/json", "{\"error\":\"Failed to serialize probe schedule\"}");
    }
    return request->reply(200, "application/json", responseBuffer);
}

esp_err_t handlePutProbeDelayStop(PsychicRequest *request) {
    sendToTeensy(CMD_STOP_DELAY_PROBE, nullptr);
    return request->reply(200, "application/json", "{\"status\":\"ok\"}");
}

// Parse an optional numeric query parameter into `out`; false (and a 400
// message in `err`) when present but malformed or out of [lo, hi].
static bool parseNumParam(PsychicRequest *request, const char* name,
                          double lo, double hi, double* out, String& err) {
    if (!request->hasParam(name)) return true;
    String raw = request->getParam(name)->value();
    char* end = nullptr;
    double parsed = strtod(raw.c_str(), &end);
    if (raw.length() == 0 || end == nullptr || *end != '\0' || parsed < lo || parsed > hi) {
        err = String(name) + " must be a number in " + String(lo, 0) + ".." + String(hi, 0);
        return false;
    }
    *out = parsed;
    return true;
}

esp_err_t handlePutProbeSweepStart(PsychicRequest *request) {
    double level = 50;
    double f0 = SWEEP_DEFAULT_F0_HZ;
    double f1 = SWEEP_DEFAULT_F1_HZ;
    double chirpSamples = SWEEP_DEFAULT_CHIRP_SAMPLES;
    double passes = SWEEP_DEFAULT_N_PASSES;
    String err;
    if (!parseNumParam(request, "level", 0, 100, &level, err) ||
        !parseNumParam(request, "f0", 5, 20000, &f0, err) ||
        !parseNumParam(request, "f1", 100, 22050, &f1, err) ||
        !parseNumParam(request, "chirpSamples", 8192, 1048576, &chirpSamples, err) ||
        !parseNumParam(request, "passes", 1, SWEEP_MAX_PASSES, &passes, err)) {
        return request->reply(400, "text/plain", err.c_str());
    }
    if (f0 >= f1) {
        return request->reply(400, "text/plain", "f0 must be below f1");
    }

    // Ascending enabled outputs of the active preset, repeated `passes`
    // times on the Teensy - unlike the delay probe there is no reversed
    // second half (pass-to-pass comparison of the same output is the
    // wizard's drift/consistency check).
    int mask = 0;
    int forward[NUM_OUTPUTS];
    int count = 0;
    {
        ConfigLock lock;
        const Preset& preset = current_config.presets[current_config.active_preset_index];
        for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
            if (preset.outputs[ch].enabled) {
                mask |= 1 << ch;
                forward[count++] = ch;
            }
        }
    }
    if (count == 0) {
        return request->reply(400, "text/plain", "Active preset has no enabled outputs");
    }

    char maskStr[8], levelStr[8], f0Str[12], f1Str[12], lenStr[12], passStr[4];
    snprintf(maskStr, sizeof(maskStr), "%d", mask);
    snprintf(levelStr, sizeof(levelStr), "%d", (int)level);
    snprintf(f0Str, sizeof(f0Str), "%.1f", f0);
    snprintf(f1Str, sizeof(f1Str), "%.1f", f1);
    snprintf(lenStr, sizeof(lenStr), "%lu", (unsigned long)chirpSamples);
    snprintf(passStr, sizeof(passStr), "%d", (int)passes);
    sendToTeensy(CMD_START_SWEEP_PROBE, maskStr, levelStr, f0Str, f1Str, lenStr, passStr);

    // The authoritative schedule (preRoll/spacing/fade, on the Teensy's
    // clock) arrives in the SWEEP START probeEvent - this response only
    // confirms what was requested and in what order outputs will play.
    JsonDocument doc;
    doc["status"] = "ok";
    doc["sampleRate"] = PROBE_SAMPLE_RATE;
    doc["f0"] = f0;
    doc["f1"] = f1;
    doc["chirpSamples"] = (uint32_t)chirpSamples;
    doc["passes"] = (int)passes;
    doc["level"] = (int)level;
    JsonArray order = doc.createNestedArray("order");
    for (int i = 0; i < count; i++) order.add(forward[i]);

    char responseBuffer[384];
    size_t len = serializeJson(doc, responseBuffer, sizeof(responseBuffer));
    if (len == 0 || len >= sizeof(responseBuffer)) {
        return request->reply(500, "application/json", "{\"error\":\"Failed to serialize sweep schedule\"}");
    }
    return request->reply(200, "application/json", responseBuffer);
}

esp_err_t handlePutProbeSweepStop(PsychicRequest *request) {
    sendToTeensy(CMD_STOP_DELAY_PROBE, nullptr); // stops either probe kind
    return request->reply(200, "application/json", "{\"status\":\"ok\"}");
}
