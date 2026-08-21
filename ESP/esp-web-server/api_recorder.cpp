#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>
#include "globals.h"
#include "config.h"
#include "teensy_comm.h"
#include "api_recorder.h"

using namespace ArduinoJson;

// A recording name travels as one token of a "playRecording <name>" UART
// line: bare filename, no paths, no dotfiles, no spaces or control
// characters, and short enough for the message buffer. Mirrors the Teensy's
// own validation (validRecordingName in fir_filters.ino).
static bool isValidRecordingName(const String& name) {
    if (name.length() == 0 || name.length() >= 48) return false;
    if (name[0] == '.') return false;
    for (size_t i = 0; i < name.length(); i++) {
        unsigned char c = name[i];
        if (c <= ' ' || c == 0x7F || c == '/' || c == '\\') return false;
    }
    return true;
}

esp_err_t handleGetRecorder(PsychicRequest *request) {
    // Like the FIR list: served from the async cache, refreshed behind the
    // response so the next fetch is fresh.
    requestRecordingsRefresh();

    RecorderState state;
    getRecorderState(state);

    JsonDocument doc;
    doc["sdPresent"] = state.sdPresent;
    JsonObject rec = doc.createNestedObject("recording");
    rec["active"] = state.recording;
    rec["file"] = state.recordFile;
    rec["seconds"] = state.recordSeconds;
    JsonObject play = doc.createNestedObject("playback");
    play["active"] = state.playing;
    play["file"] = state.playFile;
    play["seconds"] = state.playSeconds;
    play["length"] = state.playLength;

    // Cache lines are "name bytes seconds" (strtok needs a mutable copy;
    // this handler runs on both server tasks, so keep it on the stack)
    char listCopy[1024];
    copyCachedRecordings(listCopy, sizeof(listCopy));
    JsonArray files = doc.createNestedArray("files");
    char* saveptr = nullptr;
    for (char* line = strtok_r(listCopy, "\n", &saveptr); line != nullptr;
         line = strtok_r(nullptr, "\n", &saveptr)) {
        char* sizeTok = strchr(line, ' ');
        if (sizeTok == nullptr) continue;
        *sizeTok++ = '\0';
        char* secsTok = strchr(sizeTok, ' ');
        if (secsTok != nullptr) *secsTok++ = '\0';
        JsonObject f = files.createNestedObject();
        f["name"] = line;
        f["size"] = strtoul(sizeTok, nullptr, 10);
        f["seconds"] = secsTok != nullptr ? strtoul(secsTok, nullptr, 10) : 0;
    }

    String out;
    serializeJson(doc, out);
    return request->reply(200, "application/json", out.c_str());
}

esp_err_t handlePostRecordStart(PsychicRequest *request) {
    RecorderState state;
    getRecorderState(state);
    if (!state.sdPresent) {
        return request->reply(409, "text/plain", "No SD card in the Teensy");
    }
    sendToTeensy(CMD_START_RECORDING, nullptr);
    return request->reply(200, "application/json", "{\"status\":\"requested\"}");
}

esp_err_t handlePostRecordStop(PsychicRequest *request) {
    sendToTeensy(CMD_STOP_RECORDING, nullptr);
    return request->reply(200, "application/json", "{\"status\":\"requested\"}");
}

esp_err_t handlePostRecorderPlay(PsychicRequest *request) {
    if (!request->hasParam("name")) {
        return request->reply(400, "text/plain", "Missing name parameter");
    }
    String name = request->getParam("name")->value();
    if (!isValidRecordingName(name)) {
        return request->reply(400, "text/plain", "Invalid recording name");
    }
    if (isRecordingActive()) {
        return request->reply(409, "text/plain", "Recording in progress");
    }
    sendToTeensy(CMD_PLAY_RECORDING, name.c_str());
    return request->reply(200, "application/json", "{\"status\":\"requested\"}");
}

esp_err_t handlePostRecorderPlayStop(PsychicRequest *request) {
    sendToTeensy(CMD_STOP_PLAYBACK, nullptr);
    return request->reply(200, "application/json", "{\"status\":\"requested\"}");
}

esp_err_t handleDeleteRecording(PsychicRequest *request) {
    if (!request->hasParam("name")) {
        return request->reply(400, "text/plain", "Missing name parameter");
    }
    String name = request->getParam("name")->value();
    if (!isValidRecordingName(name)) {
        return request->reply(400, "text/plain", "Invalid recording name");
    }
    if (isRecordingActive()) {
        return request->reply(409, "text/plain", "Recording in progress");
    }
    sendToTeensy(CMD_DELETE_RECORDING, name.c_str());
    return request->reply(200, "application/json", "{\"status\":\"requested\"}");
}
