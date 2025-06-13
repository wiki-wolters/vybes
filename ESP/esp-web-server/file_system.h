#ifndef FILE_SYSTEM_H
#define FILE_SYSTEM_H

#include <ArduinoJson.h>

void initLittleFS();
void loadSystemSettings();
void saveSystemSettings();
bool savePreset(const String& presetName, const String& presetData);
bool deletePreset(const String& presetName);
String getPreset(const String& presetName);

void copyFile(const char* sourcePath, const char* destPath);

#endif
