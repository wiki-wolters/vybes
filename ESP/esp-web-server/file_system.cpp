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
