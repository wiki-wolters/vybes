#ifndef GLOBALS_H
#define GLOBALS_H

#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Wire.h>

// On the ESP32 the Teensy link lives on UART2, so debug output gets the USB
// port (UART0) to itself again. See docs/WIRING.md.
#define DebugSerial Serial

extern bool configChanged;

// Function Prototypes for main file
void scheduleConfigWrite();

#endif
