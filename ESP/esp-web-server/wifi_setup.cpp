// WiFiManager quarantine: this file must not include (directly or via other
// project headers) anything that pulls in ESPAsyncWebServer.h - see wifi_setup.h.
#include "globals.h"
#include "wifi_setup.h"
#define WM_MDNS 1
#include <WiFiManager.h>

void setupWiFi() {
    // Scoped locally so its RAM is released once WiFi is up
    WiFiManager wifiManager;
    // WiFiManager logs to Serial by default - that's the Teensy link now
    wifiManager.setDebugOutput(false);
    wifiManager.setConnectTimeout(20); // 20 seconds
    wifiManager.setAPCallback([](WiFiManager * myWiFiManager) {
        DebugSerial.println("Entered config mode");
    });
    wifiManager.setConfigPortalTimeout(300);
    if (!wifiManager.autoConnect("Vybes-Config")) {
        ESP.restart();
    }
    DebugSerial.println("WiFi connected!");
}
