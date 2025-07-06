#ifndef GLOBALS_H
#define GLOBALS_H

#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>
#define WM_ASYNCWEBSERVER 1
#define WM_MDNS 1
#define ESP_WM_NO_WEBSERVER 1
#include <WiFiManager.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Wire.h>

// Server and WebSocket setup
extern WiFiManager wifiManager;


extern bool configChanged;

// Function Prototypes for main file
void scheduleConfigWrite();

#endif
