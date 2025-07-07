#include "I2CCommandRouter.h"
#include <Wire.h>

// Static instance pointer for I2C callback
I2CCommandRouter* I2CCommandRouter::instance = nullptr;

I2CCommandRouter::I2CCommandRouter(uint8_t i2cAddress) 
    : commandCount(0), address(i2cAddress), delimiter(' '), responseLength(0) {
    instance = this;
    buffer.reserve(128); // Increased buffer size for commands and args
    memset(responseBuffer, 0, sizeof(responseBuffer));
}

void I2CCommandRouter::begin() {
    Wire.setSCL(19);
    Wire.setSDA(18);
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
            Serial.print("Running command: "); Serial.println(cmd_str);
            Serial.print("With args:"); Serial.println(argsString);
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
    Serial.print("Command not found: "); Serial.println(cmd_str);
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
    Serial.print("I2C received ");
    Serial.print(bytes);
    Serial.print(" bytes, buffer length before: ");
    Serial.println(buffer.length());

    //Loop i from 1 to bytes
    for (int i = 0; i < bytes; i++) {
        char c = Wire.read();
        buffer += c;
    }

    Serial.print("Buffer value: ");
    Serial.println(buffer.substring(0, buffer.length() - 1));
    Serial.print("Buffer length: ");
    Serial.println(buffer.length());

    if (buffer.endsWith("\n")) {
        //Process command
        I2CCommandRouterBufferStream stream(responseBuffer, sizeof(responseBuffer) - 1);
        processCommand(buffer.trim(), stream);
        responseLength = stream.length;
    } else {
        Serial.println("Buffer does not end with newline, discarding");
    }

    //Reset buffer
    buffer = "";
}

void I2CCommandRouter::i2cReceiveWrapper(int bytes) {
    if (instance) {
        instance->handleI2CReceive(bytes);
    } else {
        Serial.println("No command router instance found in i2cReceiveWrapper");
    }
}

// Static wrapper for I2C onRequest event
void I2CCommandRouter::i2cRequestWrapper() {
    if (instance) {
        instance->handleI2CRequest();
    } else {
        Serial.println("No command router instance found in i2cRequestWrapper");
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