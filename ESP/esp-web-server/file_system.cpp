#include "globals.h"
#include <LittleFS.h>

void copyFile(const char* sourcePath, const char* destPath) {
  File sourceFile = LittleFS.open(sourcePath, "r");
  if (!sourceFile) {
    return;
  }
  File destFile = LittleFS.open(destPath, "w");
  if (!destFile) {
    sourceFile.close();
    return;
  }
  uint8_t buf[512];
  while (sourceFile.available()) {
    size_t len = sourceFile.read(buf, sizeof(buf));
    destFile.write(buf, len);
  }
  sourceFile.close();
  destFile.close();
}
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
    if (configFile) {
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, configFile);
        if (error) {
            Serial.println("Failed to parse config file, using defaults");
        } else {
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
        }
        configFile.close();
    } else {
        Serial.println("Failed to open config file for reading, using defaults");
    }

    // Scan for presets
    systemSettings.numPresets = 0;
    if (!LittleFS.exists("/presets")) {
        LittleFS.mkdir("/presets");
    }

    Dir presetsDir = LittleFS.openDir("/presets");
    while (presetsDir.next() && systemSettings.numPresets < MAX_PRESETS) {
        String fileName = presetsDir.fileName();
        if (fileName.endsWith(".json")) {
            String presetName = fileName.substring(0, fileName.length() - 5);
            strncpy(systemSettings.presets[systemSettings.numPresets].name, presetName.c_str(), MAX_PRESET_NAME_LENGTH - 1);
            systemSettings.presets[systemSettings.numPresets].name[MAX_PRESET_NAME_LENGTH - 1] = '\0';
            systemSettings.numPresets++;
        }
    }
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
