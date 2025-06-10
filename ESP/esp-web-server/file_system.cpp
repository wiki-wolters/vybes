#include "globals.h"
#include "file_system.h"

void initLittleFS() {
    if (!LittleFS.begin()) {
        Serial.println("LittleFS mount failed, formatting...");
        LittleFS.format();
        if (!LittleFS.begin()) {
            Serial.println("LittleFS format failed!");
            ESP.restart();
        }
    }
}

void loadSystemSettings() {
    File configFile = LittleFS.open("/config.json", "r");
    if (!configFile) {
        Serial.println("Failed to open config file for reading, using defaults");
        return;
    }

    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, configFile);
    if (error) {
        Serial.println("Failed to parse config file, using defaults");
        return;
    }

    systemSettings.calibrationSpl = doc["calibrationSpl"] | 85;
    systemSettings.isCalibrated = doc["isCalibrated"] | false;
    systemSettings.subwooferState = doc["subwooferState"] | "on";
    systemSettings.bypassState = doc["bypassState"] | "off";
    systemSettings.muteState = doc["muteState"] | "off";
    systemSettings.mutePercent = doc["mutePercent"] | 0;
    systemSettings.toneFrequency = doc["toneFrequency"] | 1000;
    systemSettings.toneVolume = doc["toneVolume"] | 50;
    systemSettings.noiseVolume = doc["noiseVolume"] | 0;
    systemSettings.currentPreset = doc["currentPreset"] | "";

    configFile.close();
}

void saveSystemSettings() {
    File configFile = LittleFS.open("/config.json", "w");
    if (!configFile) {
        Serial.println("Failed to open config file for writing");
        return;
    }

    DynamicJsonDocument doc(1024);
    doc["calibrationSpl"] = systemSettings.calibrationSpl;
    doc["isCalibrated"] = systemSettings.isCalibrated;
    doc["subwooferState"] = systemSettings.subwooferState;
    doc["bypassState"] = systemSettings.bypassState;
    doc["muteState"] = systemSettings.muteState;
    doc["mutePercent"] = systemSettings.mutePercent;
    doc["toneFrequency"] = systemSettings.toneFrequency;
    doc["toneVolume"] = systemSettings.toneVolume;
    doc["noiseVolume"] = systemSettings.noiseVolume;
    doc["currentPreset"] = systemSettings.currentPreset;

    if (serializeJson(doc, configFile) == 0) {
        Serial.println("Failed to write to config file");
    }
    configFile.close();
}


bool savePreset(const String& presetName, const String& presetData) {
    File presetFile = LittleFS.open("/presets/" + presetName + ".json", "w");
    if (!presetFile) {
        return false;
    }
    presetFile.print(presetData);
    presetFile.close();
    return true;
}

bool deletePreset(const String& presetName) {
    String path = "/presets/" + presetName + ".json";
    if (LittleFS.exists(path)) {
        return LittleFS.remove(path);
    }
    return false;
}

String getPreset(const String& presetName) {
    File presetFile = LittleFS.open("/presets/" + presetName + ".json", "r");
    if (!presetFile) {
        return "";
    }
    String presetData = presetFile.readString();
    presetFile.close();
    return presetData;
}
