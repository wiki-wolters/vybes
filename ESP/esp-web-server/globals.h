#ifndef GLOBALS_H
#define GLOBALS_H

#include <ESP8266WiFi.h>
#define WM_ASYNCWEBSERVER 1
#define WM_MDNS 1
#define ESP_WM_NO_WEBSERVER 1
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Wire.h>

// UART0 (Serial) is the Teensy link after the swap() in setup, so all debug
// output goes to Serial1: TX-only on GPIO2 (D4). See docs/WIRING.md.
#define DebugSerial Serial1

extern bool configChanged;

// Function Prototypes for main file
void scheduleConfigWrite();

#endif
