#ifndef SERIAL_COMMAND_ROUTER_H
#define SERIAL_COMMAND_ROUTER_H

#include <Arduino.h>
#include <functional>
#include "OutputStream.h"

// Routes newline-delimited text commands arriving on a hardware serial port
// (the link to the ESP8266) to registered handlers. Handlers may write a
// reply directly to the port through the provided OutputStream.
//
// Wire protocol (all lines newline-terminated):
//   ESP -> Teensy:  "<command> <arg1> <arg2> ...\n"
//   Teensy -> ESP:  free-form reply lines written by handlers, e.g.
//                   "PONG <uptime>", or "FILES" ... "EOT" for the file list.
//                   "EVENT <name>" lines announce unsolicited events (sendEvent).

// OutputStream backed directly by the serial port.
class SerialOutputStream : public OutputStream {
public:
    explicit SerialOutputStream(HardwareSerial& port) : port(port) {}

    size_t write(uint8_t c) override {
        return port.write(c);
    }

    size_t write(const char* data, size_t len) override {
        return port.write((const uint8_t*)data, len);
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        return port.write(buffer, size);
    }

private:
    HardwareSerial& port;
};

class SerialCommandRouter {
public:
    using Handler = std::function<void(const String&, String*, int, OutputStream&)>;

    explicit SerialCommandRouter(HardwareSerial& port);

    // Initialise the port. Call once in setup().
    void begin(uint32_t baud);

    // Register a command handler
    void on(const String& command, Handler handler);

    // Read incoming bytes and dispatch complete lines. Call from loop().
    void loop();

    // Announce an unsolicited event to the ESP, e.g. sendEvent("boot").
    void sendEvent(const char* name);

    // Parse and dispatch a raw command line (exposed for testing)
    void processCommand(const String& rawCommand, OutputStream& output);

    // Split a space-separated argument string; runs of spaces count as one
    // delimiter. Returns a new[]'d array (caller deletes) and sets count,
    // or nullptr with count 0. (Exposed for testing.)
    String* parseArgs(const String& argsString, int& count);

    static const int LINE_BUFFER_SIZE = 256;

private:
    static const int MAX_COMMANDS = 24;

    struct Command {
        String name;
        Handler handler;
    };

    HardwareSerial& port;
    SerialOutputStream output;
    Command commands[MAX_COMMANDS];
    int commandCount;

    char lineBuffer[LINE_BUFFER_SIZE];
    size_t lineLength;
    bool lineOverflow;
};

#endif // SERIAL_COMMAND_ROUTER_H
