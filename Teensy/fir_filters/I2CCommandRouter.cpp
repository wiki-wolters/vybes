#include "I2CCommandRouter.h"

// Static instance pointer for I2C callback
I2CCommandRouter* I2CCommandRouter::instance = nullptr;

I2CCommandRouter::I2CCommandRouter(uint8_t i2cAddress) 
    : commandCount(0), address(i2cAddress), delimiter(' ') {
    instance = this;
    buffer.reserve(128); // Increased buffer size for commands and args
}

void I2CCommandRouter::begin() {
    Wire.begin(address);
    Wire.onReceive(i2cReceiveWrapper);
    Wire.onRequest(i2cRequestWrapper); // Corrected to use the static wrapper
}

void I2CCommandRouter::setDelimiter(char delimiter) {
    this->delimiter = delimiter;
}

void I2CCommandRouter::on(const String& commandName, CommandHandler handler) {
    if (commandCount < MAX_COMMANDS) {
        commands[commandCount].name = commandName;
        commands[commandCount].handler = handler;
        commandCount++;
    } else {
        // Optional: Add error handling for too many commands
        // For example, Serial.println("Error: Maximum commands reached");
    }
}

void I2CCommandRouter::processCommand(const String& rawCommand) {
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
            
            // Call the handler with command name, args array, and count
            commands[i].handler(cmd_str, args, argCount);
            
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
    while (Wire.available()) {
        char c = Wire.read();
        if (c == '\n' || c == '\r') {
            if (buffer.length() > 0) {
                processCommand(buffer);
                buffer = "";
            }
        } else {
            buffer += c;
        }
    }
    
    // Process command if no newline was sent
    if (buffer.length() > 0) {
        processCommand(buffer);
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
    // This function is called when a master requests data from this slave device.
    // You need to implement the logic to send data back to the master.
    // For example, Wire.write("hello\n");
    // Or send data from a buffer prepared by one of your commands.
    // This is a placeholder.
    Wire.write("ACK"); // Send a simple acknowledgment
}