#include "globals.h"
#include "config.h"
#include "web_server.h"
#include "websocket.h"
#include "file_system.h"
#include "teensy_comm.h"
#include "utilities.h"
#include "i2c.h"
#include "screen.h"
#include "button.h"

// Define global objects
WebSocketsServer webSocket(8080);
WiFiManager wifiManager;
// SystemSettings systemSettings; // This is now replaced by the global 'current_config' object.
bool configChanged = false;
unsigned long lastConfigChange = 0;
const unsigned long WRITE_DELAY = 5000;

void setupWiFi() {
    wifiManager.setConnectTimeout(20); // 20 seconds
    wifiManager.setAPCallback([](WiFiManager * myWiFiManager) {
        Serial.println("Entered config mode");
    });
    wifiManager.setConfigPortalTimeout(300);
    if (!wifiManager.autoConnect("Vybes-Config")) {
        ESP.restart();
    }
    Serial.println("WiFi connected!");
}

void handleDebounceWrite() {
    if (configChanged && (millis() - lastConfigChange > WRITE_DELAY)) {
        Serial.println("Debouncing: Saving configuration ...");
        save_config();
        configChanged = false;
    }
}

void scheduleConfigWrite() {
    configChanged = true;
    lastConfigChange = millis();
}

void setup() {
    Serial.begin(115200);
    setupButton();
    initLittleFS(); // For serving web files

    setupWiFi();
    initI2C();
    setupScreen();
    writeToScreen("Vybes DSP");

    if (MDNS.begin("vybes")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("mDNS responder started: vybes.local");
    }

    setupWebServer();
    setupWebSocket();

    init_config();  // Load configuration

    Serial.println("Vybes DSP ready!");
    Serial.println(WiFi.localIP());
    Serial.printf("Free heap: %d\n", ESP.getFreeHeap());
}

void loop() {
    webSocket.loop();
    MDNS.update();
    handleDebounceWrite();
    handleButton();
    //loopScreen();
}