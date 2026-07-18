#ifndef NATIVE_SHIM_ARDUINO_H
#define NATIVE_SHIM_ARDUINO_H

// Minimal host-side stand-in for the Arduino/Teensy core, providing just
// what the firmware sources compiled into the native test build need
// (String, Print, HardwareSerial, Serial). Selected by [env:native]'s
// -Itest/native_shim include flag; the real Teensy build never sees it.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>

#include "WString.h"
#include "Print.h"

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif

// Serial port stand-in: tests feed the RX side with feedInput() and inspect
// everything the code under test wrote via the 'output' string.
class HardwareSerial : public Print {
public:
    void begin(unsigned long) {}

    int available() { return (int)(input.size() - inputPos); }

    int read() {
        if (inputPos >= input.size()) return -1;
        return (uint8_t)input[inputPos++];
    }

    size_t write(uint8_t b) override {
        output += (char)b;
        return 1;
    }
    using Print::write;

    int availableForWrite() { return 4096; }

    // --- test helpers ---
    void feedInput(const char* data) { input.append(data); }
    void feedInput(const char* data, size_t len) { input.append(data, len); }
    void clear() { input.clear(); inputPos = 0; output.clear(); }

    std::string input;
    size_t inputPos = 0;
    std::string output;
};

// Debug console stand-in: swallows writes, keeps them for inspection.
class NativeUsbSerial : public Print {
public:
    size_t write(uint8_t b) override {
        log += (char)b;
        return 1;
    }
    using Print::write;
    std::string log;
};

// One instance per translation unit is fine here: only the tests themselves
// assert on serial contents, and they use their own HardwareSerial objects.
static NativeUsbSerial Serial __attribute__((unused));

#endif // NATIVE_SHIM_ARDUINO_H
