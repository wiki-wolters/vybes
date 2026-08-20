#include "globals.h"
#include "api_volume.h"
#include "config.h"
#include "api_helpers.h"
#include "teensy_comm.h"
#include "utilities.h"
#include "websocket.h"
#include <ArduinoJson.h>

// Master volume is a per-preset value (config.h): each preset remembers the
// level it was last played at, and only the active preset's value is on the
// Teensy. Every write goes through applyVolume so the Teensy push, the save
// and the broadcast can't drift apart between the API, the IR remote and the
// front-panel button.

// Broadcast which preset's volume changed, so a UI showing a different
// preset doesn't take the value as its own.
static void broadcastVolumeChanged(const Preset& preset) {
    JsonDocument doc;
    doc["messageType"] = "volumeChanged";
    doc["presetName"] = preset.name;
    doc["volume"] = preset.volume;
    // Roomy enough for a max-length preset name with JSON escaping
    char messageBuffer[192];
    size_t len = serializeJson(doc, messageBuffer, sizeof(messageBuffer));
    if (len > 0 && len < sizeof(messageBuffer)) {
        broadcastWebSocket(messageBuffer);
    } else {
        DebugSerial.println("Error serializing JSON for WebSocket broadcast or buffer too small.");
    }
}

/**
 * Store a preset's volume and, when it is the preset being played, push it
 * to the Teensy. Returns the clamped value.
 */
static int applyVolume(int presetIndex, int volume) {
    volume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    Preset& preset = current_config.presets[presetIndex];
    {
        ConfigLock lock;
        preset.volume = volume;
        scheduleConfigWrite();
    }
    if (presetIndex == current_config.active_preset_index) {
        sendFloatToTeensy(CMD_SET_VOLUME, volume / 100.0f);
    }
    broadcastVolumeChanged(preset);
    return volume;
}

// PUT /volume?value=0-100[&preset_name=] - without preset_name this is the
// live master volume (the active preset's); naming a preset edits the level
// it will play at without touching what is playing now.
esp_err_t handlePutVolume(PsychicRequest *request) {
    if (!request->hasParam("value")) {
        return request->reply(400, "application/json",
                              "{\"success\":false,\"error\":\"Missing value parameter\"}");
    }

    int volume = request->getParam("value")->value().toInt();
    if (volume < 0 || volume > 100) {
        return request->reply(400, "application/json",
                              "{\"success\":false,\"error\":\"Volume must be between 0 and 100\"}");
    }

    int presetIndex = current_config.active_preset_index;
    if (request->hasParam("preset_name")) {
        String presetName = request->getParam("preset_name")->value();
        presetIndex = find_preset_by_name(presetName.c_str());
        if (presetIndex == -1) {
            return request->reply(404, "application/json",
                                  "{\"success\":false,\"error\":\"Preset not found\"}");
        }
    }

    volume = applyVolume(presetIndex, volume);

    JsonDocument doc;
    doc["success"] = true;
    doc["presetName"] = current_config.presets[presetIndex].name;
    doc["volume"] = volume;
    String response;
    serializeJson(doc, response);
    return request->reply(200, "application/json", response.c_str());
}

void increase_volume(int amount) {
    if (active_preset().volume < 100) {
        applyVolume(current_config.active_preset_index, active_preset().volume + amount);
    }
}

void decrease_volume(int amount) {
    if (active_preset().volume > 0) {
        applyVolume(current_config.active_preset_index, active_preset().volume - amount);
    }
}

void toggle_mute() {
    current_config.muted = !current_config.muted;
    sendOnOffToTeensy(CMD_SET_MUTE, current_config.muted);
    scheduleConfigWrite();
    // Prepare data for WebSocket broadcast
    JsonDocument doc;
    doc["messageType"] = "muteChanged";
    doc["muted"] = current_config.muted;
    char messageBuffer[128]; // Adjust size as needed
    size_t len = serializeJson(doc, messageBuffer, sizeof(messageBuffer));
    if (len > 0 && len < sizeof(messageBuffer)) {
        broadcastWebSocket(messageBuffer);
    } else {
        DebugSerial.println("Error serializing JSON for WebSocket broadcast or buffer too small.");
    }
}
