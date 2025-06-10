#include "globals.h"
#include "web_server.h"
#include "websocket.h"
#include "file_system.h"
#include "teensy_comm.h"
#include "utilities.h"

// Web server handlers
void handleGetCalibration();
void handlePutCalibrate();
void handleGetStatus();
void handlePutSub();
void handlePutBypass();
void handlePutMute();
void handlePutMutePercent();
void handlePutTone();
void handlePutToneStop();
void handlePutNoise();
void handlePutPulse();
void handleGetPresets();
void handleGetPreset();
void handlePostPresetCreate();
void handlePostPresetCopy();
void handlePutPresetRename();
void handleDeletePreset();
void handlePutPresetActive();
void handlePutSpeakerDelay();
void handlePutSpeakerDelayNamed();
void handlePostEQ();
void handleDeleteEQ();
void handlePutCrossover();
void handlePutEqualLoudness();
void handleNotFound();
void handleFileServing();
void handlePutActivePreset();
void handlePutPresetCrossover();
void handlePutPresetEqualLoudness();
void handleDeletePresetEQ();
void handlePostPresetEQ();
void handlePutPresetDelayNamed();
void handlePutPresetDelay();

void setupWebServer() {
    server.enableCORS(true);

    // API Routes - Calibration
    server.on("/calibration", HTTP_GET, handleGetCalibration);
    server.on(UriRegex("/calibrate/(.+)"), HTTP_PUT, handlePutCalibrate);

    // API Routes - System Status
    server.on("/status", HTTP_GET, handleGetStatus);
    server.on(UriRegex("/sub/(.+)"), HTTP_PUT, handlePutSub);
    server.on(UriRegex("/bypass/(.+)"), HTTP_PUT, handlePutBypass);
    server.on(UriRegex("/mute/(.+)"), HTTP_PUT, handlePutMute);
    server.on(UriRegex("/mute/percent/(.+)"), HTTP_PUT, handlePutMutePercent);

    // API Routes - Tone Generation
    server.on(UriRegex("/generate/tone/(.+)/(.+)"), HTTP_PUT, handlePutTone);
    server.on("/generate/tone/stop", HTTP_PUT, handlePutToneStop);
    server.on(UriRegex("/generate/noise/(.+)"), HTTP_PUT, handlePutNoise);
    server.on("/pulse", HTTP_PUT, handlePutPulse);

    // API Routes - Preset Management
    server.on("/presets", HTTP_GET, handleGetPresets);
    server.on(UriRegex("/preset/(.+)"), HTTP_GET, handleGetPreset);
    server.on(UriRegex("/preset/create/(.+)"), HTTP_POST, handlePostPresetCreate);
    server.on(UriRegex("/preset/copy/(.+)/(.+)"), HTTP_POST, handlePostPresetCopy);
    server.on(UriRegex("/preset/rename/(.+)/(.+)"), HTTP_PUT, handlePutPresetRename);
    server.on(UriRegex("/preset/(.+)"), HTTP_DELETE, handleDeletePreset);
    server.on(UriRegex("/preset/active/(.+)"), HTTP_PUT, handlePutActivePreset);

    // API Routes - Speaker Configuration
    server.on(UriRegex("/preset/delay/(.+)/(.+)"), HTTP_PUT, handlePutPresetDelay);
    server.on(UriRegex("/preset/(.+)/delay/(.+)/(.+)"), HTTP_PUT, handlePutPresetDelayNamed);

    // API Routes - EQ Management
    server.on(UriRegex("/preset/(.+)/eq/(.+)/(.+)"), HTTP_POST, handlePostPresetEQ);
    server.on(UriRegex("/preset/(.+)/eq/(.+)/(.+)"), HTTP_DELETE, handleDeletePresetEQ);

    // API Routes - Crossover and Equal Loudness
    server.on(UriRegex("/preset/(.+)/crossover/(.+)/(.+)"), HTTP_PUT, handlePutPresetCrossover);
    server.on(UriRegex("/preset/(.+)/equal-loudness/(.+)"), HTTP_PUT, handlePutPresetEqualLoudness);

    // Static file serving
    server.onNotFound(handleFileServing);

    server.begin();
    Serial.println("HTTP server started on port 80");
}

// API Handler Implementations

void handleGetCalibration() {
    DynamicJsonDocument doc(256);
    doc["isCalibrated"] = systemSettings.isCalibrated;
    if (systemSettings.isCalibrated) {
        doc["spl"] = systemSettings.calibrationSpl;
    }

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handlePutCalibrate() {
    String splStr = server.pathArg(0);
    int spl = splStr.toInt();

    if (spl < 60 || spl > 100) {
        server.send(400, "text/plain", "Invalid SPL value");
        return;
    }

    systemSettings.calibrationSpl = spl;
    systemSettings.isCalibrated = true;
    scheduleConfigWrite();

    DynamicJsonDocument doc(256);
    doc["isCalibrated"] = systemSettings.isCalibrated;
    doc["spl"] = systemSettings.calibrationSpl;

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);

    broadcastWebSocket(response);
}

void handleGetStatus() {
    DynamicJsonDocument doc(1024);
    doc["subwooferState"] = systemSettings.subwooferState;
    doc["bypassState"] = systemSettings.bypassState;
    doc["muteState"] = systemSettings.muteState;
    doc["mutePercent"] = systemSettings.mutePercent;
    doc["toneFrequency"] = systemSettings.toneFrequency;
    doc["toneVolume"] = systemSettings.toneVolume;
    doc["noiseVolume"] = systemSettings.noiseVolume;
    doc["currentPreset"] = systemSettings.currentPreset;

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handlePutSub() {
    String state = server.pathArg(0);
    if (state != "on" && state != "off") {
        server.send(400, "text/plain", "Invalid state");
        return;
    }

    systemSettings.subwooferState = state;
    scheduleConfigWrite();

    sendToTeensy("sub", state);

    DynamicJsonDocument doc(256);
    doc["subwooferState"] = systemSettings.subwooferState;

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);

    broadcastWebSocket(response);
}

void handlePutBypass() {
    String state = server.pathArg(0);
    if (state != "on" && state != "off") {
        server.send(400, "text/plain", "Invalid state");
        return;
    }

    systemSettings.bypassState = state;
    scheduleConfigWrite();

    sendToTeensy("bypass", state);

    DynamicJsonDocument doc(256);
    doc["bypassState"] = systemSettings.bypassState;

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);

    broadcastWebSocket(response);
}

void handlePutMute() {
    String state = server.pathArg(0);
    if (state != "on" && state != "off") {
        server.send(400, "text/plain", "Invalid state");
        return;
    }

    systemSettings.muteState = state;
    scheduleConfigWrite();

    sendToTeensy("mute", state);

    DynamicJsonDocument doc(256);
    doc["muteState"] = systemSettings.muteState;

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);

    broadcastWebSocket(response);
}

void handlePutMutePercent() {
    String percentStr = server.pathArg(0);
    int percent = percentStr.toInt();

    if (percent < 0 || percent > 100) {
        server.send(400, "text/plain", "Invalid percent value");
        return;
    }

    systemSettings.mutePercent = percent;
    scheduleConfigWrite();

    sendToTeensy("mute_percent", String(percent));

    DynamicJsonDocument doc(256);
    doc["mutePercent"] = systemSettings.mutePercent;

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);

    broadcastWebSocket(response);
}

void handlePutTone() {
    String freqStr = server.pathArg(0);
    String volStr = server.pathArg(1);
    int freq = freqStr.toInt();
    int vol = volStr.toInt();

    if (freq < 20 || freq > 20000) {
        server.send(400, "text/plain", "Invalid frequency value");
        return;
    }
    if (vol < 0 || vol > 100) {
        server.send(400, "text/plain", "Invalid volume value");
        return;
    }

    systemSettings.toneFrequency = freq;
    systemSettings.toneVolume = vol;
    scheduleConfigWrite();

    sendToTeensy("tone", String(freq) + "," + String(vol));

    DynamicJsonDocument doc(256);
    doc["toneFrequency"] = systemSettings.toneFrequency;
    doc["toneVolume"] = systemSettings.toneVolume;

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);

    broadcastWebSocket(response);
}

void handlePutToneStop() {
    systemSettings.toneVolume = 0;
    scheduleConfigWrite();

    sendToTeensy("tone_stop", "");

    DynamicJsonDocument doc(256);
    doc["toneVolume"] = systemSettings.toneVolume;

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);

    broadcastWebSocket(response);
}

void handlePutNoise() {
    String volStr = server.pathArg(0);
    int vol = volStr.toInt();

    if (vol < 0 || vol > 100) {
        server.send(400, "text/plain", "Invalid volume value");
        return;
    }

    systemSettings.noiseVolume = vol;
    scheduleConfigWrite();

    sendToTeensy("noise", String(vol));

    DynamicJsonDocument doc(256);
    doc["noiseVolume"] = systemSettings.noiseVolume;

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);

    broadcastWebSocket(response);
}

void handlePutPulse() {
    sendToTeensy("pulse", "");
    server.send(200, "text/plain", "Pulse sent");
}

void handleGetPresets() {
    DynamicJsonDocument doc(1024);
    JsonArray presets = doc.to<JsonArray>();

    Dir dir = LittleFS.openDir("/presets");
    while (dir.next()) {
        String filename = dir.fileName();
        if (filename.endsWith(".json")) {
            presets.add(filename.substring(0, filename.length() - 5));
        }
    }

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleGetPreset() {
    String presetName = urlDecode(server.pathArg(0));
    String presetData = getPreset(presetName);

    if (presetData.length() > 0) {
        server.send(200, "application/json", presetData);
    } else {
        server.send(404, "text/plain", "Preset not found");
    }
}

void handlePostPresetCreate() {
    String presetName = urlDecode(server.pathArg(0));

    if (presetName.length() == 0) {
        server.send(400, "text/plain", "Preset name is required");
        return;
    }

    String presetFilename = "/presets/" + presetName + ".json";
    if (LittleFS.exists(presetFilename)) {
        server.send(409, "text/plain", "Preset already exists");
        return;
    }

    DynamicJsonDocument doc(2048);
    if (server.hasArg("plain")) {
        DeserializationError error = deserializeJson(doc, server.arg("plain"));
        if (error) {
            server.send(400, "text/plain", "Invalid JSON");
            return;
        }
    } else {
        // Create a default preset structure
        JsonObject crossover = doc.createNestedObject("crossover");
        crossover["lowpass_frequency"] = 80;
        crossover["highpass_frequency"] = 80;

        JsonObject equalLoudness = doc.createNestedObject("equalLoudness");
        equalLoudness["enabled"] = false;

        JsonObject delay = doc.createNestedObject("delay");
        delay["left"] = 0;
        delay["right"] = 0;
        delay["sub"] = 0;

        JsonObject roomCorrection = doc.createNestedObject("roomCorrection");
        JsonArray roomCorrectionSets = roomCorrection.createNestedArray("sets");
        JsonObject defaultRoomSet = roomCorrectionSets.createNestedObject();
        defaultRoomSet["spl"] = 0;
        defaultRoomSet.createNestedArray("peq");

        JsonObject preferenceCurve = doc.createNestedObject("preferenceCurve");
        JsonArray preferenceCurveSets = preferenceCurve.createNestedArray("sets");
        JsonObject defaultPrefSet = preferenceCurveSets.createNestedObject();
        defaultPrefSet["spl"] = 0;
        defaultPrefSet.createNestedArray("peq");
    }

    String presetData;
    serializeJson(doc, presetData);

    if (savePreset(presetName, presetData)) {
        server.send(201, "application/json", presetData);
    } else {
        server.send(500, "text/plain", "Failed to save preset");
    }
}

void handlePostPresetCopy() {
    String sourceName = urlDecode(server.pathArg(0));
    String destName = urlDecode(server.pathArg(1));

    if (sourceName.length() == 0 || destName.length() == 0) {
        server.send(400, "text/plain", "Source and destination names are required");
        return;
    }

    String sourceFilename = "/presets/" + sourceName + ".json";
    String destFilename = "/presets/" + destName + ".json";

    if (!LittleFS.exists(sourceFilename)) {
        server.send(404, "text/plain", "Source preset not found");
        return;
    }

    if (LittleFS.exists(destFilename)) {
        server.send(409, "text/plain", "Destination preset already exists");
        return;
    }

    File sourceFile = LittleFS.open(sourceFilename, "r");
    File destFile = LittleFS.open(destFilename, "w");

    if (!sourceFile || !destFile) {
        server.send(500, "text/plain", "Failed to open files");
        return;
    }

    while (sourceFile.available()) {
        destFile.write(sourceFile.read());
    }

    sourceFile.close();
    destFile.close();

    server.send(201, "text/plain", "Preset copied");
}

void handlePutPresetRename() {
    String oldName = urlDecode(server.pathArg(0));
    String newName = urlDecode(server.pathArg(1));

    if (oldName.length() == 0 || newName.length() == 0) {
        server.send(400, "text/plain", "Old and new names are required");
        return;
    }

    String oldFilename = "/presets/" + oldName + ".json";
    String newFilename = "/presets/" + newName + ".json";

    if (!LittleFS.exists(oldFilename)) {
        server.send(404, "text/plain", "Preset not found");
        return;
    }

    if (LittleFS.exists(newFilename)) {
        server.send(409, "text/plain", "New preset name already exists");
        return;
    }

    if (LittleFS.rename(oldFilename, newFilename)) {
        if (systemSettings.currentPreset == oldName) {
            systemSettings.currentPreset = newName;
            scheduleConfigWrite();
        }
        server.send(200, "text/plain", "Preset renamed");
    } else {
        server.send(500, "text/plain", "Failed to rename preset");
    }
}

void handleDeletePreset() {
    String presetName = urlDecode(server.pathArg(0));

    if (presetName.length() == 0) {
        server.send(400, "text/plain", "Preset name is required");
        return;
    }

    if (deletePreset(presetName)) {
        if (systemSettings.currentPreset == presetName) {
            systemSettings.currentPreset = "";
            scheduleConfigWrite();
            // TODO: Unload active preset from Teensy
        }
        server.send(200, "text/plain", "Preset deleted");
    } else {
        server.send(404, "text/plain", "Preset not found");
    }
}

void handlePutPresetDelay() {
    String presetName = urlDecode(server.pathArg(0));
    String delayStr = server.pathArg(1);

    DynamicJsonDocument doc(2048);
    String presetData = getPreset(presetName);
    if (presetData.length() == 0) {
        server.send(404, "text/plain", "Preset not found");
        return;
    }

    DeserializationError error = deserializeJson(doc, presetData);
    if (error) {
        server.send(500, "text/plain", "Failed to parse preset data");
        return;
    }

    JsonObject delay = doc["delay"];
    if (delay.isNull()) {
        delay = doc.createNestedObject("delay");
    }

    // Assuming delay string is "left,right,sub"
    int l, r, s;
    sscanf(delayStr.c_str(), "%d,%d,%d", &l, &r, &s);
    delay["left"] = l;
    delay["right"] = r;
    delay["sub"] = s;

    serializeJson(doc, presetData);
    if (savePreset(presetName, presetData)) {
        if (systemSettings.currentPreset == presetName) {
            sendToTeensy("delay", delayStr);
        }
        server.send(200, "application/json", presetData);
    } else {
        server.send(500, "text/plain", "Failed to save preset");
    }
}

void handlePutPresetDelayNamed() {
    String presetName = urlDecode(server.pathArg(0));
    String speaker = server.pathArg(1);
    String delayMs = server.pathArg(2);

    DynamicJsonDocument doc(2048);
    String presetData = getPreset(presetName);
    if (presetData.length() == 0) {
        server.send(404, "text/plain", "Preset not found");
        return;
    }

    DeserializationError error = deserializeJson(doc, presetData);
    if (error) {
        server.send(500, "text/plain", "Failed to parse preset data");
        return;
    }

    JsonObject delay = doc["delay"];
    if (delay.isNull()) {
        delay = doc.createNestedObject("delay");
    }

    delay[speaker] = delayMs.toFloat();

    serializeJson(doc, presetData);
    if (savePreset(presetName, presetData)) {
        if (systemSettings.currentPreset == presetName) {
            sendToTeensy("delay_" + speaker, delayMs);
        }
        server.send(200, "application/json", presetData);
    } else {
        server.send(500, "text/plain", "Failed to save preset");
    }
}

void handlePostPresetEQ() {
    String presetName = urlDecode(server.pathArg(0));
    String eqType = server.pathArg(1);
    String splStr = server.pathArg(2);
    int spl = splStr.toInt();

    if (eqType != "roomCorrection" && eqType != "preferenceCurve") {
        server.send(400, "text/plain", "Invalid EQ type");
        return;
    }

    DynamicJsonDocument doc(4096);
    String presetData = getPreset(presetName);
    if (presetData.length() == 0) {
        server.send(404, "text/plain", "Preset not found");
        return;
    }

    DeserializationError error = deserializeJson(doc, presetData);
    if (error) {
        server.send(500, "text/plain", "Failed to parse preset data");
        return;
    }

    JsonObject eqSection = doc[eqType];
    if (eqSection.isNull()) {
        eqSection = doc.createNestedObject(eqType);
    }

    JsonArray sets = eqSection["sets"];
    if (sets.isNull()) {
        sets = eqSection.createNestedArray("sets");
    }

    // Find if a set with this SPL already exists
    JsonObject setToUpdate;
    for (JsonObject set : sets) {
        if (set["spl"] == spl) {
            setToUpdate = set;
            break;
        }
    }

    if (setToUpdate.isNull()) {
        // Create new set
        setToUpdate = sets.createNestedObject();
        setToUpdate["spl"] = spl;
    }

    JsonArray peq = setToUpdate["peq"];
    if (peq.isNull()) {
        peq = setToUpdate.createNestedArray("peq");
    }

    DynamicJsonDocument newPeqData(2048);
    DeserializationError jsonError = deserializeJson(newPeqData, server.arg("plain"));
    if (jsonError) {
        server.send(400, "text/plain", "Invalid JSON body");
        return;
    }

    peq.clear();
    for (JsonVariant value : newPeqData.as<JsonArray>()) {
        peq.add(value);
    }

    serializeJson(doc, presetData);
    if (savePreset(presetName, presetData)) {
        if (systemSettings.currentPreset == presetName) {
            // TODO: Send updated EQ to Teensy
        }
        server.send(200, "application/json", presetData);
    } else {
        server.send(500, "text/plain", "Failed to save preset");
    }
}

void handleDeletePresetEQ() {
    String presetName = urlDecode(server.pathArg(0));
    String eqType = server.pathArg(1);
    String splStr = server.pathArg(2);
    int spl = splStr.toInt();

    if (eqType != "roomCorrection" && eqType != "preferenceCurve") {
        server.send(400, "text/plain", "Invalid EQ type");
        return;
    }

    DynamicJsonDocument doc(4096);
    String presetData = getPreset(presetName);
    if (presetData.length() == 0) {
        server.send(404, "text/plain", "Preset not found");
        return;
    }

    DeserializationError error = deserializeJson(doc, presetData);
    if (error) {
        server.send(500, "text/plain", "Failed to parse preset data");
        return;
    }

    JsonObject eqSection = doc[eqType];
    if (eqSection.isNull()) {
        server.send(404, "text/plain", "EQ type not found in preset");
        return;
    }

    JsonArray sets = eqSection["sets"];
    if (sets.isNull()) {
        server.send(404, "text/plain", "No EQ sets found");
        return;
    }

    bool found = false;
    for (size_t i = 0; i < sets.size(); i++) {
        if (sets[i]["spl"] == spl) {
            sets.remove(i);
            found = true;
            break;
        }
    }

    if (!found) {
        server.send(404, "text/plain", "EQ set with specified SPL not found");
        return;
    }

    serializeJson(doc, presetData);
    if (savePreset(presetName, presetData)) {
        if (systemSettings.currentPreset == presetName) {
            // TODO: Send updated EQ to Teensy
        }
        server.send(200, "application/json", presetData);
    } else {
        server.send(500, "text/plain", "Failed to save preset");
    }
}

void handlePutPresetCrossover() {
    String presetName = urlDecode(server.pathArg(0));
    String type = server.pathArg(1);
    String freq = server.pathArg(2);

    DynamicJsonDocument doc(2048);
    String presetData = getPreset(presetName);
    if (presetData.length() == 0) {
        server.send(404, "text/plain", "Preset not found");
        return;
    }

    DeserializationError error = deserializeJson(doc, presetData);
    if (error) {
        server.send(500, "text/plain", "Failed to parse preset data");
        return;
    }

    JsonObject crossover = doc["crossover"];
    if (crossover.isNull()) {
        crossover = doc.createNestedObject("crossover");
    }

    if (type == "lowpass") {
        crossover["lowpass_frequency"] = freq.toInt();
    } else if (type == "highpass") {
        crossover["highpass_frequency"] = freq.toInt();
    } else {
        server.send(400, "text/plain", "Invalid crossover type");
        return;
    }

    serializeJson(doc, presetData);
    if (savePreset(presetName, presetData)) {
        if (systemSettings.currentPreset == presetName) {
            sendToTeensy("crossover_" + type, freq);
        }
        server.send(200, "application/json", presetData);
    } else {
        server.send(500, "text/plain", "Failed to save preset");
    }
}

void handlePutPresetEqualLoudness() {
    String presetName = urlDecode(server.pathArg(0));
    String enabled = server.pathArg(1);

    DynamicJsonDocument doc(2048);
    String presetData = getPreset(presetName);
    if (presetData.length() == 0) {
        server.send(404, "text/plain", "Preset not found");
        return;
    }

    DeserializationError error = deserializeJson(doc, presetData);
    if (error) {
        server.send(500, "text/plain", "Failed to parse preset data");
        return;
    }

    JsonObject equalLoudness = doc["equalLoudness"];
    if (equalLoudness.isNull()) {
        equalLoudness = doc.createNestedObject("equalLoudness");
    }

    equalLoudness["enabled"] = (enabled == "true");

    serializeJson(doc, presetData);
    if (savePreset(presetName, presetData)) {
        if (systemSettings.currentPreset == presetName) {
            sendToTeensy("equal_loudness", enabled);
        }
        server.send(200, "application/json", presetData);
    } else {
        server.send(500, "text/plain", "Failed to save preset");
    }
}

void handlePutActivePreset() {
    String presetName = urlDecode(server.pathArg(0));

    if (presetName.length() > 0 && !LittleFS.exists("/presets/" + presetName + ".json")) {
        server.send(404, "text/plain", "Preset not found");
        return;
    }

    systemSettings.currentPreset = presetName;
    scheduleConfigWrite();

    // TODO: Load preset into Teensy
    if (presetName.length() > 0) {
        String presetData = getPreset(presetName);
        // Parse and send to Teensy
    }

    DynamicJsonDocument doc(256);
    doc["currentPreset"] = systemSettings.currentPreset;

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);

    broadcastWebSocket(response);
}

void handleFileServing() {
    String path = server.uri();
    if (path.endsWith("/")) {
        path += "index.html";
    }
    String contentType = "text/html";
    if (path.endsWith(".css")) {
        contentType = "text/css";
    } else if (path.endsWith(".js")) {
        contentType = "application/javascript";
    } else if (path.endsWith(".ico")) {
        contentType = "image/x-icon";
    }

    if (LittleFS.exists(path)) {
        File file = LittleFS.open(path, "r");
        server.streamFile(file, contentType);
        file.close();
    } else {
        server.send(404, "text/plain", "Not Found");
    }
}
