#include "globals.h"
#include "web_server.h"
#include "websocket.h"
#include "file_system.h"
#include "utilities.h"
#include "teensy_comm.h"

void handlePutPresetDelay(AsyncWebServerRequest *request) {
    String speaker = request->pathArg(0);
    String delayStr = request->pathArg(1);
    float delay = delayStr.toFloat();

    String presetName = systemSettings.currentPreset;
    String filePath = "/presets/" + String(presetName) + ".json";
    if (!LittleFS.exists(filePath)) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    File file = LittleFS.open(filePath, "r+");
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, file);

    doc["delay"][speaker] = delay;

    file.seek(0);
    serializeJson(doc, file);
    file.close();

    sendToTeensy("delay", speaker + "," + delayStr);

    request->send(200, "application/json", "{}");
}

void handlePutPresetDelayNamed(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    String speaker = request->pathArg(1);
    String delayStr = request->pathArg(2);
    float delay = delayStr.toFloat();

    String filePath = "/presets/" + presetName + ".json";
    if (!LittleFS.exists(filePath)) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    File file = LittleFS.open(filePath, "r+");
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, file);

    doc["delay"][speaker] = delay;

    file.seek(0);
    serializeJson(doc, file);
    file.close();

    sendToTeensy("delay_" + speaker, delayStr);

    request->send(200, "application/json", "{}");
}

void handlePostPresetEQ(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    String eqType = request->pathArg(1);
    String splStr = request->pathArg(2);
    int spl = splStr.toInt();

    if (eqType != "roomCorrection" && eqType != "preferenceCurve") {
        request->send(400, "text/plain", "Invalid EQ type");
        return;
    }

    String filePath = "/presets/" + presetName + ".json";
    if (!LittleFS.exists(filePath)) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    File file = LittleFS.open(filePath, "r+");
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, file);

    JsonArray eqArray = doc[eqType].as<JsonArray>();

    // Check if SPL already exists
    for (JsonObject obj : eqArray) {
        if (obj["spl"] == spl) {
            request->send(409, "text/plain", "SPL value already exists");
            return;
        }
    }

    JsonObject newSet = eqArray.createNestedObject();
    newSet["spl"] = spl;
    newSet.createNestedArray("points");

    file.seek(0);
    serializeJson(doc, file);
    file.close();

    request->send(201, "application/json", "{}");
}

void handleDeletePresetEQ(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    String eqType = request->pathArg(1);
    String splStr = request->pathArg(2);
    int spl = splStr.toInt();

    if (eqType != "roomCorrection" && eqType != "preferenceCurve") {
        request->send(400, "text/plain", "Invalid EQ type");
        return;
    }

    String filePath = "/presets/" + presetName + ".json";
    if (!LittleFS.exists(filePath)) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    File file = LittleFS.open(filePath, "r+");
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, file);

    JsonArray eqArray = doc[eqType].as<JsonArray>();
    JsonArray newArray = doc.createNestedArray();

    bool found = false;
    for (JsonObject obj : eqArray) {
        if (obj["spl"] == spl) {
            found = true;
        } else {
            newArray.add(obj);
        }
    }

    if (!found) {
        file.close();
        request->send(404, "text/plain", "SPL value not found");
        return;
    }

    doc[eqType] = newArray;

    file.seek(0);
    serializeJson(doc, file);
    file.close();

    request->send(204, "");
}

void handlePutPresetCrossover(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    String type = request->pathArg(1);
    String freqStr = request->pathArg(2);
    int freq = freqStr.toInt();

    if (type != "lowPass" && type != "highPass") {
        request->send(400, "text/plain", "Invalid crossover type");
        return;
    }

    String filePath = "/presets/" + presetName + ".json";
    if (!LittleFS.exists(filePath)) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    File file = LittleFS.open(filePath, "r+");
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, file);

    doc["crossover"][type] = freq;

    file.seek(0);
    serializeJson(doc, file);
    file.close();

    sendToTeensy("crossover_" + type, freqStr);

    request->send(200, "application/json", "{}");
}

void handlePutPresetEqualLoudness(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    String state = request->pathArg(1);

    if (state != "on" && state != "off") {
        request->send(400, "text/plain", "Invalid state");
        return;
    }

    String filePath = "/presets/" + presetName + ".json";
    if (!LittleFS.exists(filePath)) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    File file = LittleFS.open(filePath, "r+");
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, file);

    doc["equalLoudness"] = (state == "on");

    file.seek(0);
    serializeJson(doc, file);
    file.close();

    sendToTeensy("equal_loudness", state);

    request->send(200, "application/json", "{}");
}
