#include "I2CCommandRouter.h"

// Static instance pointer for I2C callback
I2CCommandRouter* I2CCommandRouter::instance = nullptr;

I2CCommandRouter::I2CCommandRouter(uint8_t i2cAddress) 
    : commandCount(0), address(i2cAddress), delimiter(' '), responseLength(0) {
    instance = this;
    buffer.reserve(128); // Increased buffer size for commands and args
    memset(responseBuffer, 0, sizeof(responseBuffer));
}

void I2CCommandRouter::begin() {
    Wire.begin(address);
    Wire.onReceive(i2cReceiveWrapper);
    Wire.onRequest(i2cRequestWrapper); // Corrected to use the static wrapper
}

void I2CCommandRouter::setDelimiter(char delimiter) {
    this->delimiter = delimiter;
}

void I2CCommandRouter::on(const String& commandName, I2CHandler handler) {
    if (commandCount < MAX_COMMANDS) {
        commands[commandCount].name = commandName;
        commands[commandCount].handler = handler;
        commandCount++;
    } else {
        // Optional: Add error handling for too many commands
        // For example, Serial.println("Error: Maximum commands reached");
    }
}

void I2CCommandRouter::processCommand(const String& rawCommand, OutputStream& output) {
    String cmd_str; // Renamed to avoid conflict with member 'commands'
    String argsString;
    
    int firstDelim = rawCommand.indexOf(delimiter);
    if (firstDelim == -1) {
        cmd_str = rawCommand;
        argsString = "";
    } else {
        cmd_str = rawCommand.substring(0, firstDelim);
        argsString = rawCommand.substring(firstDelim + 1);
    }
    
    cmd_str.trim();
    argsString.trim();
    
    // Find matching command
    for (int i = 0; i < commandCount; i++) {
        if (commands[i].name.equalsIgnoreCase(cmd_str)) {
            int argCount;
            String* args = parseArgs(argsString, argCount);
            
            // Call the handler with command name, args array, count, and output stream
            commands[i].handler(cmd_str, args, argCount, output);
            
            // Clean up
            if (argCount > 0 && args != nullptr) {
                delete[] args;
            }
            return;
        }
    }
    
    // Command not found - could add error handling here if needed
    // For example, Serial.print("Command not found: "); Serial.println(cmd_str);
}

String* I2CCommandRouter::parseArgs(const String& argsString, int& count) {
    if (argsString.length() == 0) {
        count = 0;
        return nullptr;
    }
    
    // Count delimiters to determine array size
    count = 1;
        for (unsigned int i = 0; i < argsString.length(); i++) {
        if (argsString.charAt(i) == delimiter) count++;
    }
    
    String* result = new String[count];
    int start = 0;
    int index = 0;
    
        for (unsigned int i = 0; i <= argsString.length(); i++) {
        if (i == argsString.length() || argsString.charAt(i) == delimiter) {
            result[index] = argsString.substring(start, i);
            result[index].trim();
            start = i + 1;
            index++;
        }
    }
    
    return result;
}

void I2CCommandRouter::handleI2CReceive(int bytes) {
    buffer = "";
    responseLength = 0; // Reset response buffer
    
    while (Wire.available()) {
        char c = Wire.read();
        if (c == '\n' || c == '\r') {
            if (buffer.length() > 0) {
                // Create a buffer stream for the output
                I2CCommandRouterBufferStream stream(responseBuffer, sizeof(responseBuffer) - 1);
                processCommand(buffer, stream);
                responseLength = stream.length;
                buffer = "";
            }
        } else {
            buffer += c;
        }
    }
    
    // Process command if no newline was sent
    if (buffer.length() > 0) {
        I2CCommandRouterBufferStream stream(responseBuffer, sizeof(responseBuffer) - 1);
        processCommand(buffer, stream);
        responseLength = stream.length;
        buffer = "";
    }
}

void I2CCommandRouter::i2cReceiveWrapper(int bytes) {
    if (instance) {
        instance->handleI2CReceive(bytes);
    }
}

// Static wrapper for I2C onRequest event
void I2CCommandRouter::i2cRequestWrapper() {
    if (instance) {
        instance->handleI2CRequest();
    }
}

// Instance method to handle I2C onRequest event
void I2CCommandRouter::handleI2CRequest() {
    // Send the response buffer back to the master
    if (responseLength > 0) {
        Wire.write(responseBuffer, responseLength);
        // Null-terminate the buffer for good measure
        if (responseLength < sizeof(responseBuffer)) {
            responseBuffer[responseLength] = '\0';
        }
    } else {
        // Send a simple acknowledgment if no response was prepared
        Wire.write("OK\n");
    }
    
    // Reset the response buffer for the next command
    responseLength = 0;
}