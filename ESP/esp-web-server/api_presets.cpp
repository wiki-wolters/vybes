#include "globals.h"
#include "web_server.h"
#include "websocket.h"
#include "file_system.h"
#include "utilities.h"
#include "teensy_comm.h"

void handleGetPresets(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(1024);
    JsonArray presets = doc.to<JsonArray>();
    for (int i = 0; i < systemSettings.numPresets; i++) {
        presets.add(systemSettings.presets[i].name);
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleGetPreset(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);
    String filePath = "/presets/" + presetName + ".json";

    if (!LittleFS.exists(filePath)) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    File file = LittleFS.open(filePath, "r");
    request->send(LittleFS, filePath, "application/json");
    file.close();
}

void handlePostPresetCreate(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);

    if (presetName.length() == 0 || presetName.length() > 32) {
        request->send(400, "text/plain", "Invalid preset name");
        return;
    }

    for (int i = 0; i < systemSettings.numPresets; i++) {
        if (strcmp(systemSettings.presets[i].name, presetName.c_str()) == 0) {
            request->send(409, "text/plain", "Preset name already exists");
            return;
        }
    }

    if (systemSettings.numPresets >= MAX_PRESETS) {
        request->send(507, "text/plain", "Maximum number of presets reached");
        return;
    }

    // Create new preset file
    String filePath = "/presets/" + presetName + ".json";
    DynamicJsonDocument doc(2048);
    doc["name"] = presetName;
    // Set default values
    JsonObject delay = doc.createNestedObject("delay");
    delay["left"] = 0;
    delay["right"] = 0;
    delay["sub"] = 0;

    JsonArray roomCorrection = doc.createNestedArray("roomCorrection");
    JsonObject peqSet = roomCorrection.createNestedObject();
    peqSet["spl"] = 0;
    peqSet.createNestedArray("points");

    JsonArray preferenceCurve = doc.createNestedArray("preferenceCurve");
    JsonObject peqSet2 = preferenceCurve.createNestedObject();
    peqSet2["spl"] = 0;
    peqSet2.createNestedArray("points");

    JsonObject crossover = doc.createNestedObject("crossover");
    crossover["lowPass"] = 80;
    crossover["highPass"] = 80;

    doc["equalLoudness"] = false;

    File file = LittleFS.open(filePath, "w");
    serializeJson(doc, file);
    file.close();

    // Add to system settings
    strcpy(systemSettings.presets[systemSettings.numPresets].name, presetName.c_str());
    systemSettings.numPresets++;
    scheduleConfigWrite();

    request->send(201, "application/json", "{}");
}

void handlePostPresetCopy(AsyncWebServerRequest *request) {
    String sourceName = request->pathArg(0);
    String destName = request->pathArg(1);

    if (destName.length() == 0 || destName.length() > 32) {
        request->send(400, "text/plain", "Invalid destination preset name");
        return;
    }

    for (int i = 0; i < systemSettings.numPresets; i++) {
        if (strcmp(systemSettings.presets[i].name, destName.c_str()) == 0) {
            request->send(409, "text/plain", "Destination preset name already exists");
            return;
        }
    }

    if (systemSettings.numPresets >= MAX_PRESETS) {
        request->send(507, "text/plain", "Maximum number of presets reached");
        return;
    }

    String sourcePath = "/presets/" + sourceName + ".json";
    String destPath = "/presets/" + destName + ".json";

    if (!LittleFS.exists(sourcePath)) {
        request->send(404, "text/plain", "Source preset not found");
        return;
    }

    copyFile(sourcePath.c_str(), destPath.c_str());

    // Update name in new file
    File file = LittleFS.open(destPath, "r+");
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, file);
    doc["name"] = destName;
    file.seek(0);
    serializeJson(doc, file);
    file.close();

    // Add to system settings
    strcpy(systemSettings.presets[systemSettings.numPresets].name, destName.c_str());
    systemSettings.numPresets++;
    scheduleConfigWrite();

    request->send(201, "application/json", "{}");
}

void handlePutPresetRename(AsyncWebServerRequest *request) {
    String oldName = request->pathArg(0);
    String newName = request->pathArg(1);

    if (newName.length() == 0 || newName.length() > 32) {
        request->send(400, "text/plain", "Invalid new preset name");
        return;
    }

    for (int i = 0; i < systemSettings.numPresets; i++) {
        if (strcmp(systemSettings.presets[i].name, newName.c_str()) == 0) {
            request->send(409, "text/plain", "New preset name already exists");
            return;
        }
    }

    int presetIndex = -1;
    for (int i = 0; i < systemSettings.numPresets; i++) {
        if (strcmp(systemSettings.presets[i].name, oldName.c_str()) == 0) {
            presetIndex = i;
            break;
        }
    }

    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset to rename not found");
        return;
    }

    String oldPath = "/presets/" + oldName + ".json";
    String newPath = "/presets/" + newName + ".json";

    LittleFS.rename(oldPath.c_str(), newPath.c_str());

    // Update name in new file
    File file = LittleFS.open(newPath, "r+");
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, file);
    doc["name"] = newName;
    file.seek(0);
    serializeJson(doc, file);
    file.close();

    // Update in system settings
    strcpy(systemSettings.presets[presetIndex].name, newName.c_str());
    scheduleConfigWrite();

    request->send(200, "application/json", "{}");
}

void handleDeletePreset(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);

    int presetIndex = -1;
    for (int i = 0; i < systemSettings.numPresets; i++) {
        if (strcmp(systemSettings.presets[i].name, presetName.c_str()) == 0) {
            presetIndex = i;
            break;
        }
    }

    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    String filePath = "/presets/" + presetName + ".json";
    LittleFS.remove(filePath);

    // Remove from system settings
    for (int i = presetIndex; i < systemSettings.numPresets - 1; i++) {
        systemSettings.presets[i] = systemSettings.presets[i + 1];
    }
    systemSettings.numPresets--;
    scheduleConfigWrite();

    request->send(204, "");
}

void handlePutActivePreset(AsyncWebServerRequest *request) {
    String presetName = request->pathArg(0);

    int presetIndex = -1;
    for (int i = 0; i < systemSettings.numPresets; i++) {
        if (strcmp(systemSettings.presets[i].name, presetName.c_str()) == 0) {
            presetIndex = i;
            break;
        }
    }

    if (presetIndex == -1) {
        request->send(404, "text/plain", "Preset not found");
        return;
    }

    systemSettings.currentPreset = presetName;
    scheduleConfigWrite();

    // Load preset into Teensy
    String filePath = "/presets/" + presetName + ".json";
    File file = LittleFS.open(filePath, "r");
    String presetData = file.readString();
    file.close();
    sendToTeensy("load_preset", presetData);

    DynamicJsonDocument doc(256);
    doc["currentPreset"] = systemSettings.currentPreset;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);

    broadcastWebSocket(response);
}
