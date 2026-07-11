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
#include <ESPmDNS.h>

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
    // Debug output on USB
    DebugSerial.begin(115200);
    DebugSerial.println("\nVybes ESP starting");

    // UART2 is the Teensy link. See docs/WIRING.md.
    TeensySerial.begin(TEENSY_BAUD, SERIAL_8N1, TEENSY_RX_PIN, TEENSY_TX_PIN);

    setupButton();
    initLittleFS(); // For serving web files

    remoteControl.setup();

    bool standalone = setupWiFi();
    initI2C(); // LCD only - the Teensy link is UART now
    setupScreen();
    writeToScreen("Vybes DSP");

    if (MDNS.begin("vybes")) {
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("https", "tcp", 443);
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
    teensyCommLoop();     // Drain queued Teensy commands, read replies/events
    websocketLoop();      // RTA keepalive relay to the Teensy
    handleDebounceWrite();
    handleButton();
    loopScreen();
}
