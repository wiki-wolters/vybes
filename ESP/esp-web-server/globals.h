#ifndef GLOBALS_H
#define GLOBALS_H
#define WEBSERVER_H

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WebSocketsServer.h>
#define ESP_WM_NO_WEBSERVER 1
#include <WiFiManager.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <uri/UriRegex.h>

// Server and WebSocket setup
extern WiFiManager wifiManager;

#define MAX_PRESETS 10
#define MAX_PRESET_NAME_LENGTH 32

// Preset structure
struct Preset {
  char name[MAX_PRESET_NAME_LENGTH];
};

// System settings structure
struct SystemSettings {
    int calibrationSpl = 85;
    bool isCalibrated = false;
    String subwooferState = "on";
    String bypassState = "off";
    String muteState = "off";
    int mutePercent = 0;
    int toneFrequency = 1000;
    int toneVolume = 50;
    int noiseVolume = 0;
    String currentPreset = "";
    int numPresets = 0;
    Preset presets[MAX_PRESETS];
};

extern SystemSettings systemSettings;
extern bool configChanged;

// Function Prototypes for main file
void scheduleConfigWrite();

#endif
