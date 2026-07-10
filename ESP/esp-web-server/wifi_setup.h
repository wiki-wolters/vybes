#ifndef WIFI_SETUP_H
#define WIFI_SETUP_H

// Connects to WiFi via WiFiManager (config portal on first use). The portal
// also offers a "Standalone mode" button that skips the router entirely and
// brings up a bare access point (SSID "Vybes") for direct phone connections,
// e.g. in the car. Returns true when running standalone, false when joined
// to a network. Restarts the ESP if the portal times out with no choice made.
//
// Lives in its own translation unit because WiFiManager.h includes the
// core's ESP8266WebServer.h, whose HTTP_* method enum collides with
// ESPAsyncWebServer.h - the two headers must never meet in one file.
bool setupWiFi();

#endif // WIFI_SETUP_H
