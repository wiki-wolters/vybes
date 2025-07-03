#ifndef I2C_COMMAND_ROUTER_H
#define I2C_COMMAND_ROUTER_H
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <map>
#include <functional>
#include "OutputStream.h"

using I2CHandler = std::function<void(const String&, String*, int, OutputStream&)>;

class I2CCommandRouterBufferStream : public OutputStream {
public:
    char* buffer;
    size_t capacity;
    size_t length;
    I2CCommandRouterBufferStream(char* buf, size_t cap) : buffer(buf), capacity(cap), length(0) {}
    size_t write(const char* data, size_t len) override {
        size_t toWrite = min(len, capacity - length);
        memcpy(buffer + length, data, toWrite);
        length += toWrite;
        return toWrite;
    }
};

class I2CCommandRouter {
public:
    // Handler function that receives command name, array of arguments, and output stream
    using I2CHandler = std::function<void(const String&, String*, int, OutputStream&)>;
    
    // Constructor
    I2CCommandRouter(uint8_t i2cAddress);
    
    // Initialize I2C and set up receive handler
    void begin();
    
    // Register a command handler
    void on(const String& command, I2CHandler handler);
    
    // Manual processing (if you want to handle I2C events manually)
    void processCommand(const String& rawCommand, OutputStream& output);
    
    // Set custom delimiter (default is space)
    void setDelimiter(char delimiter);
    
private:
    struct Command {
        String name;
        I2CHandler handler;
    };
    
    static const int MAX_COMMANDS = 20;
    Command commands[MAX_COMMANDS];
    int commandCount;
    uint8_t address;
    char delimiter;
    String buffer;
    
    // Internal methods
    void handleI2CReceive(int bytes);
    String* parseArgs(const String& args, int& count);
    
    // Buffer for I2C response
    char responseBuffer[256];
    size_t responseLength;
    
    // Static wrapper for I2C callback
    static I2CCommandRouter* instance;
    static void i2cReceiveWrapper(int bytes);
    static void i2cRequestWrapper(); // Added for Wire.onRequest

    void handleI2CRequest(); // Added for Wire.onRequest
};

#endif