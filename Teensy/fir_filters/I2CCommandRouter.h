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
    I2CCommandRouterBufferStream(char* buf, size_t cap) : buffer(buf), capacity(cap), length(0) {
        if (capacity > 0) buffer[0] = '\0';
    }

    // Required by Print class
    size_t write(uint8_t c) override {
        if (length < capacity - 1) {
            buffer[length++] = c;
            buffer[length] = '\0'; // Keep it null-terminated
            return 1;
        }
        return 0;
    }

    // Overload for writing a buffer, used by the parent OutputStream
    size_t write(const char* data, size_t len) override {
        size_t toWrite = min(len, capacity - length - 1);
        if (toWrite > 0) {
          memcpy(buffer + length, data, toWrite);
          length += toWrite;
          buffer[length] = '\0'; // Keep it null-terminated
        }
        return toWrite;
    }

    // This is the standard Print::write for buffers
    size_t write(const uint8_t *buffer, size_t size) override {
        return write((const char *)buffer, size);
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
    
    // Call this in your main loop to process commands
    void loop();
    
    // Manual processing (if you want to handle I2C events manually)
    void processCommand(const String& rawCommand, OutputStream& output);
    
    // Set custom delimiter (default is space)
    void setDelimiter(char delimiter);
    
private:
    struct Command {
        String name;
        I2CHandler handler;
    };
    
    // Static constants for array sizes
    static const int MAX_COMMANDS = 20;
    static const int I2C_BUFFER_SIZE = 32;
    static const int MAX_I2C_REQUESTS = 5;
    
    // Response status enum
    enum ResponseStatus {
        STATUS_OK,
        STATUS_ERROR,
        STATUS_COMMAND_NOT_FOUND,
        STATUS_BUFFER_OVERFLOW,
        STATUS_INVALID_COMMAND
    };

    // I2C Request structure
    struct I2CRequest {
        int8_t bytesReceived;
        char message[I2C_BUFFER_SIZE];
        bool inUse;
    };
    
    // Member variables - organized by initialization order
    int commandCount;
    uint8_t address;
    char delimiter;
    
    // ISR-safe double buffer system
    volatile char i2cBuffer[I2C_BUFFER_SIZE];
    volatile char processingBuffer[I2C_BUFFER_SIZE];
    volatile int bufferIndex;
    volatile bool commandReady;
    volatile bool bufferOverflow;
    volatile bool isProcessing;
    
    // I2C request queue
    I2CRequest i2cRequests[MAX_I2C_REQUESTS];
    volatile int8_t i2cRequestWriteIndex;  // Points to next available slot
    volatile int8_t i2cRequestReadIndex;   // Points to next request to process
    
    // Response handling
    char responseBuffer[256];
    size_t responseLength;
    volatile ResponseStatus lastStatus;
    
    // Command storage
    Command commands[MAX_COMMANDS];
    
    // Working buffer (used outside ISR)
    String workingBuffer;
    
    // Internal methods
    void handleI2CReceive(int bytes);
    String* parseArgs(const String& args, int& count);
    void processIncomingCommand();
    void handleI2CRequest();
    
    // Static wrapper for I2C callback
    static I2CCommandRouter* instance;
    static void i2cReceiveWrapper(int bytes);
    static void i2cRequestWrapper();
};

#endif