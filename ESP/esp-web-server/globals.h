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


extern bool configChanged;

// Function Prototypes for main file
void scheduleConfigWrite();

#endif
