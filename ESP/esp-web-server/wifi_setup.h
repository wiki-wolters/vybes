#ifndef WIFI_SETUP_H
#define WIFI_SETUP_H

// Connects to WiFi via WiFiManager (config portal on first use).
// Restarts the ESP if no connection could be made.
//
// Lives in its own translation unit because WiFiManager.h includes the
// core's ESP8266WebServer.h, whose HTTP_* method enum collides with
// ESPAsyncWebServer.h - the two headers must never meet in one file.
void setupWiFi();

#endif // WIFI_SETUP_H
