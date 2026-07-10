// WiFiManager quarantine: this file must not include (directly or via other
// project headers) anything that pulls in ESPAsyncWebServer.h - see wifi_setup.h.
#include "globals.h"
#include "wifi_setup.h"
#define WM_MDNS 1
#include <WiFiManager.h>

// Standalone AP (no router, e.g. in the car): the phone joins this network
// and reaches the web UI at http://192.168.4.1. WPA2 needs >= 8 characters.
static const char *STANDALONE_SSID = "Vybes";
static const char *STANDALONE_PASSWORD = "vybes-dsp";

static bool standaloneRequested = false;

bool setupWiFi() {
    // Scoped locally so its RAM is released once WiFi is up
    WiFiManager wifiManager;
    // WiFiManager logs to Serial by default - that's the Teensy link now
    wifiManager.setDebugOutput(false);
    wifiManager.setConnectTimeout(20); // 20 seconds
    wifiManager.setAPCallback([](WiFiManager * myWiFiManager) {
        DebugSerial.println("Entered config mode");
    });
    wifiManager.setConfigPortalTimeout(300);
    // Non-blocking so the loop below can also watch for the standalone button
    wifiManager.setConfigPortalBlocking(false);

    // "custom" is where setCustomMenuHTML content lands in the portal menu
    std::vector<const char *> menu = {"wifi", "custom", "info", "exit"};
    wifiManager.setMenu(menu);
    wifiManager.setCustomMenuHTML(
        "<form action='/standalone' method='get'>"
        "<button>Standalone mode (no router)</button></form><br/>");
    wifiManager.setWebServerCallback([&wifiManager]() {
        wifiManager.server->on("/standalone", [&wifiManager]() {
            wifiManager.server->send(200, "text/html",
                "<html><body><h1>Standalone mode</h1>"
                "<p>Join the <b>Vybes</b> WiFi network (password: <b>vybes-dsp</b>), "
                "keep the connection when your phone warns there is no internet, "
                "then open <a href='http://192.168.4.1'>http://192.168.4.1</a>.</p>"
                "</body></html>");
            standaloneRequested = true;
        });
    });

    if (wifiManager.autoConnect("Vybes-Config")) {
        DebugSerial.println("WiFi connected!");
        return false;
    }

    // The config portal is now up (non-blocking). Pump it until credentials
    // get us connected, the standalone button is pressed, or it times out.
    while (wifiManager.getConfigPortalActive() && !standaloneRequested) {
        wifiManager.process();
        yield();
    }

    if (standaloneRequested) {
        wifiManager.stopConfigPortal();
        WiFi.mode(WIFI_AP);
        // Plain AP, deliberately without a captive-portal DNS server: phones
        // must see their internet probes fail so they keep routing internet
        // traffic (Spotify etc.) over cellular while joined to this network.
        WiFi.softAP(STANDALONE_SSID, STANDALONE_PASSWORD);
        DebugSerial.print("Standalone AP up: ");
        DebugSerial.println(WiFi.softAPIP());
        return true;
    }

    if (WiFi.status() == WL_CONNECTED) {
        DebugSerial.println("WiFi connected!");
        return false;
    }

    // Portal timed out with no choice made - start over, the router may just
    // not be back up yet (e.g. after a power cut)
    ESP.restart();
    return false; // not reached
}
