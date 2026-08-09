#include "globals.h"
#include "compare_mode.h"
#include "config.h"
#include "teensy_comm.h"
#include "teensy_protocol.h"
#include "websocket.h"
#include <math.h>

// Same keepalive scheme as the RTA relay in websocket.cpp
#define COMPARE_CLIENT_TIMEOUT_MS 5000
#define COMPARE_TEENSY_REFRESH_MS 2000

static unsigned long lastClientKeepaliveAt = 0;
static bool active = false;
static float referenceLoudnessDb = 0.0f; // quietest state seen this session
static int trimCentiDb = 0;              // <= 0, what the Teensy is holding

// Set by compareOnStateChanged (any task, possibly under the config lock -
// a bool store is atomic on Xtensa/RISC-V), consumed by the loop task, which
// does the actual loudness math. That coalesces bursts (an EQ apply is ~10
// point writes -> one recompute) and adds no latency to API replies.
static volatile bool stateDirty = false;

// Pink-weighted gain of each output's loaded FIR filter, in centi-dB.
// Fed by "FIRGAIN <ch> <centi-dB>" lines (sent after every loadFirFiles and
// on request via getFirGains); zero until the Teensy reports.
static int firGainCentiDb[NUM_OUTPUTS] = {0};

void compareSetFirGain(int channel, int centiDb) {
    if (channel < 0 || channel >= NUM_OUTPUTS) return;
    if (firGainCentiDb[channel] == centiDb) return;
    firGainCentiDb[channel] = centiDb;
    // A (re)loaded FIR changes the active state's loudness
    compareOnStateChanged();
}

// Exact bell (peaking) magnitude response in dB. Same math as the Teensy's
// PEQMath.cpp and the WebUI's eq-math.js, so all three agree on the curve.
static float bellDb(float freq, float centerFreq, float gain, float q) {
    if (gain == 0.0f || q <= 0.0f || centerFreq <= 0.0f || freq <= 0.0f) return 0.0f;
    float A = powf(10.0f, gain / 40.0f);
    float O = freq / centerFreq;
    float c = 1.0f - O * O;
    c *= c;
    float nb = A * O / q;
    float db = O / (A * q);
    float num = c + nb * nb;
    float den = c + db * db;
    return 10.0f * log10f(num / den);
}

#define LOUDNESS_SAMPLES 100

// The preference-curve points of the SPL-0 set (the one the Teensy runs)
static const PEQSet* activeInputEqSet(const Preset& preset) {
    for (int i = 0; i < MAX_PEQ_SETS; i++) {
        if (preset.inputEq.sets[i].spl == 0) return &preset.inputEq.sets[i];
    }
    return nullptr;
}

// Pink-weighted loudness of the active preset in dB, relative to a flat
// unity system (constant offsets cancel when states are compared). Reads
// current_config without the lock, like the other loop-task readers
// (updateTeensyWithActivePresetParameters) - a torn read during a
// concurrent edit only skews an estimate that the edit's own dirty flag
// recomputes right after.
static float activeStateLoudnessDb() {
    const Preset& preset = current_config.presets[current_config.active_preset_index];

    // Input EQ curve, shared by every output
    float inputCurveDb[LOUDNESS_SAMPLES] = {0};
    const PEQSet* set = preset.inputEq.enabled ? activeInputEqSet(preset) : nullptr;
    for (int i = 0; i < LOUDNESS_SAMPLES; i++) {
        float freq = 20.0f * powf(1000.0f, (float)i / (LOUDNESS_SAMPLES - 1));
        if (set != nullptr) {
            for (int p = 0; p < set->num_points; p++) {
                inputCurveDb[i] += bellDb(freq, set->points[p].freq,
                                          set->points[p].gain, set->points[p].q);
            }
        }
    }

    double totalPower = 0.0;
    for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
        const Output& o = preset.outputs[ch];
        if (!o.enabled || o.mute) continue;
        // Uncorrelated L/R program: routing contributions sum in power
        float routePower = (float)(o.sourceLeft * o.sourceLeft +
                                   o.sourceRight * o.sourceRight);
        if (routePower <= 0.0f) continue;

        double bandPowerSum = 0.0;
        for (int i = 0; i < LOUDNESS_SAMPLES; i++) {
            float freq = 20.0f * powf(1000.0f, (float)i / (LOUDNESS_SAMPLES - 1));
            float curveDb = inputCurveDb[i];
            if (o.eqEnabled) {
                for (int p = 0; p < o.num_peq; p++) {
                    curveDb += bellDb(freq, o.peq[p].freq, o.peq[p].gain, o.peq[p].q);
                }
            }
            bandPowerSum += pow(10.0, curveDb / 10.0);
        }

        float flatDb = (float)o.gainDb;
        if (preset.firEnabled && o.fir[0] != '\0') {
            flatDb += firGainCentiDb[ch] / 100.0f;
        }
        totalPower += (bandPowerSum / LOUDNESS_SAMPLES) * routePower *
                      pow(10.0, flatDb / 10.0);
    }

    if (totalPower < 1e-12) return -120.0f;
    return (float)(10.0 * log10(totalPower));
}

// Push the current trim to the Teensy and tell the web clients.
static void pushTrim() {
    char cdb[12];
    snprintf(cdb, sizeof(cdb), "%d", trimCentiDb);
    sendToTeensy(CMD_SET_COMPARE_TRIM, cdb);

    char msg[96];
    snprintf(msg, sizeof(msg),
             "{\"messageType\":\"compareMode\",\"active\":%s,\"trimDb\":%.2f}",
             active ? "true" : "false", trimCentiDb / 100.0f);
    broadcastWebSocket(msg);
}

void compareModeKeepalive() {
    lastClientKeepaliveAt = millis();
}

void compareModeRelease() {
    lastClientKeepaliveAt = 0; // the loop deactivates and clears the trim
}

bool compareModeActive() {
    return active;
}

void compareOnStateChanged() {
    if (active) stateDirty = true;
}

// Recompute the trim from the (possibly new) reference. The quietest state
// seen in the session plays untrimmed; every louder state is matched down
// to it, so the trim can never boost into clipping.
static void recomputeTrim() {
    float loudness = activeStateLoudnessDb();
    if (loudness < referenceLoudnessDb) {
        referenceLoudnessDb = loudness;
    }
    int centiDb = (int)lroundf((referenceLoudnessDb - loudness) * 100.0f);
    if (centiDb > 0) centiDb = 0;
    if (centiDb < -3000) centiDb = -3000;
    if (centiDb == trimCentiDb) return;
    trimCentiDb = centiDb;
    pushTrim();
}

void compareModeLoop() {
    unsigned long now = millis();
    bool wantActive = lastClientKeepaliveAt != 0 &&
                      now - lastClientKeepaliveAt < COMPARE_CLIENT_TIMEOUT_MS;

    if (wantActive && !active) {
        active = true;
        // The state playing right now is the session's starting reference
        referenceLoudnessDb = activeStateLoudnessDb();
        trimCentiDb = 0;
        stateDirty = false;
        pushTrim();
        DebugSerial.println("Comparison mode on");
        return;
    }
    if (!wantActive && active) {
        active = false;
        trimCentiDb = 0;
        stateDirty = false;
        pushTrim(); // "setCompareTrim 0" - level ramps back to normal
        DebugSerial.println("Comparison mode off");
        return;
    }
    if (!active) return;

    if (stateDirty) {
        stateDirty = false;
        recomputeTrim();
    }

    // Refresh the Teensy-side keepalive while the mode is held
    static unsigned long lastTeensyRefreshAt = 0;
    if (now - lastTeensyRefreshAt >= COMPARE_TEENSY_REFRESH_MS) {
        lastTeensyRefreshAt = now;
        char cdb[12];
        snprintf(cdb, sizeof(cdb), "%d", trimCentiDb);
        sendToTeensy(CMD_SET_COMPARE_TRIM, cdb);
    }
}
