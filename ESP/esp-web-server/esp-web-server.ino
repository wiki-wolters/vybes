#include "globals.h"
#include "web_server.h"
#include "websocket.h"
#include "file_system.h"
#include "teensy_comm.h"

// Define global objects
ESP8266WebServer server(80);
WebSocketsServer webSocket(8080);
WiFiManager wifiManager;
SystemSettings systemSettings;
bool configChanged = false;
unsigned long lastConfigChange = 0;
const unsigned long WRITE_DELAY = 500;

void setupWiFi() {
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
        saveSystemSettings();
        configChanged = false;
    }
}

void scheduleConfigWrite() {
    configChanged = true;
    lastConfigChange = millis();
}

void setup() {
    Serial.begin(115200);
    Wire.begin();
    initLittleFS();
    loadSystemSettings();
    setupWiFi();

    if (MDNS.begin("vybes")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("mDNS responder started: vybes.local");
    }

    setupWebServer();
    setupWebSocket();

    Serial.println("Vybes DSP ready!");
    Serial.println(WiFi.localIP());
}

void loop() {
    server.handleClient();
    webSocket.loop();
    MDNS.update();
    handleDebounceWrite();
}