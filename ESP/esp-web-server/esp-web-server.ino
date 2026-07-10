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
#include "remote_control.h"
#include "wifi_setup.h"
#include <ESP8266mDNS.h>

// Define global objects
RemoteControl remoteControl;
bool configChanged = false;
unsigned long lastConfigChange = 0;
const unsigned long WRITE_DELAY = 5000;

void handleDebounceWrite() {
    if (configChanged && (millis() - lastConfigChange > WRITE_DELAY)) {
        DebugSerial.println("Debouncing: Saving configuration ...");
        save_config();
        configChanged = false;
    }
}

void scheduleConfigWrite() {
    configChanged = true;
    lastConfigChange = millis();
}

void setup() {
    // UART0 is the Teensy link: begin on the default pins, then swap it to
    // GPIO15 (D8, TX) / GPIO13 (D7, RX). See docs/WIRING.md.
    TeensySerial.begin(TEENSY_BAUD);
    TeensySerial.swap();

    // Debug output: Serial1, TX-only on GPIO2 (D4)
    DebugSerial.begin(115200);
    DebugSerial.println("\nVybes ESP starting");

    setupButton();
    initLittleFS(); // For serving web files

    remoteControl.setup();

    bool standalone = setupWiFi();
    initI2C(); // LCD only - the Teensy link is UART now
    setupScreen();
    writeToScreen("Vybes DSP");

    if (MDNS.begin("vybes")) {
        MDNS.addService("http", "tcp", 80);
        DebugSerial.println("mDNS responder started: vybes.local");
    }

    setupWebServer();
    setupWebSocket();

    initTeensyComm();
    init_config();  // Load configuration

    DebugSerial.println("Vybes DSP ready!");
    DebugSerial.println(standalone ? WiFi.softAPIP() : WiFi.localIP());
    DebugSerial.printf("Free heap: %d\n", ESP.getFreeHeap());
}

void loop() {
    remoteControl.loop();
    MDNS.update();
    teensyCommLoop();     // Drain queued Teensy commands, read replies/events
    websocketLoop();      // RTA keepalive relay to the Teensy
    handleDebounceWrite();
    handleButton();
    loopScreen();
}
