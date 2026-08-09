#include "globals.h"
#include "config.h"
#include "web_server.h"
#include "websocket.h"
#include "file_system.h"
#include "teensy_comm.h"
#include "compare_mode.h"
#include "utilities.h"
#include "i2c.h"
#include "screen.h"
#include "button.h"
#include "remote_control.h"
#include "wifi_setup.h"

// Define global objects
RemoteControl remoteControl;
bool configChanged = false;
unsigned long lastConfigChange = 0;
const unsigned long WRITE_DELAY = 5000;

void handleDebounceWrite() {
    if (configChanged && (millis() - lastConfigChange > WRITE_DELAY)) {
        DebugSerial.println("Debouncing: Saving configuration ...");
        // Clear the flag before serializing: a change landing mid-save
        // re-dirties the config and gets picked up by the next cycle,
        // instead of being lost when the flag is cleared afterwards.
        configChanged = false;
        save_config();
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

    initLittleFS(); // Config file and web assets
    initI2C(); // LCD only - the Teensy link is UART now
    setupScreen();
    writeToScreen("Vybes DSP");

    // The config must be loaded before anything captures the active preset
    // (button, IR remote) or serves it (web server). initTeensyComm comes
    // first: init_config queues the DSP sync commands.
    initTeensyComm();
    init_config();

    setupButton();
    remoteControl.setup();

    bool standalone = setupWiFi();

    // Modem power-save drops multicast while the radio sleeps, so mDNS
    // queries can go unanswered until the resolver retries. Mains-powered
    // device: trade the idle current for a radio that hears every query.
    WiFi.setSleep(false);

    // macOS/iOS resolvers query A and AAAA together and wait a full 5s for
    // the AAAA answer this responder otherwise never sends (no NSEC denial
    // either) - every vybes.local lookup stalled 5s and Safari surfaced
    // "Load failed". A link-local IPv6 address gives mDNS a real AAAA
    // record to answer with.
    if (!standalone) {
        WiFi.enableIpV6();
    }

    startMdns(); // "<deviceName>.local", see utilities.cpp

    setupWebSocket();
    setupWebServer();

    DebugSerial.println("Vybes DSP ready!");
    DebugSerial.println(standalone ? WiFi.softAPIP() : WiFi.localIP());
    DebugSerial.printf("Free heap: %d\n", ESP.getFreeHeap());
}

void loop() {
    remoteControl.loop();
    teensyCommLoop();     // Drain queued Teensy commands, read replies/events
    websocketLoop();      // RTA keepalive relay to the Teensy
    compareModeLoop();    // A/B level matching: loudness math + trim relay
    handleDebounceWrite();
    handleButton();
    loopScreen();
}
