#ifndef GLOBALS_H
#define GLOBALS_H

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WebSocketsServer.h>
#include <WiFiManager.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <uri/UriRegex.h>

// Server and WebSocket setup
extern ESP8266WebServer server;
extern WebSocketsServer webSocket;
extern WiFiManager wifiManager;

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
};

extern SystemSettings systemSettings;
extern bool configChanged;

// Function Prototypes for main file
void scheduleConfigWrite();

#endif
