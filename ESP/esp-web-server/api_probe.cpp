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
