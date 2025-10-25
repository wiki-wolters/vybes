#include "I2CCommandRouter.h"
#include <Wire.h>

// Static instance pointer for I2C callback
I2CCommandRouter* I2CCommandRouter::instance = nullptr;

I2CCommandRouter::I2CCommandRouter(uint8_t i2cAddress) 
    : commandCount(0), address(i2cAddress), delimiter(' '),
      bufferIndex(0), commandReady(false), bufferOverflow(false),
      isProcessing(false), i2cRequestWriteIndex(0), i2cRequestReadIndex(0),
      responseLength(0), lastStatus(STATUS_OK) {
    
    instance = this;
    
    // Initialize buffers
    memset((void*)i2cBuffer, 0, I2C_BUFFER_SIZE);
    memset((void*)processingBuffer, 0, I2C_BUFFER_SIZE);
    memset(responseBuffer, 0, sizeof(responseBuffer));
    
    // Initialize all requests as not in use
    for (int i = 0; i < MAX_I2C_REQUESTS; i++) {
        i2cRequests[i].inUse = false;
        i2cRequests[i].bytesReceived = 0;
        memset(i2cRequests[i].message, 0, I2C_BUFFER_SIZE);
    }
    
    // Reserve space for working buffer
    workingBuffer.reserve(128);
}

void I2CCommandRouter::begin() {
    Wire.begin(address);
    Wire.setClock(100000); // Set I2C to 100kHz (Standard Mode) for stability
    Wire.onReceive(i2cReceiveWrapper);
    Wire.onRequest(i2cRequestWrapper);
}

void I2CCommandRouter::detachInterrupts() {
  Wire.onReceive(nullptr);
  Wire.onRequest(nullptr);
}

void I2CCommandRouter::reattachInterrupts() {
  Wire.onReceive(i2cReceiveWrapper);
  Wire.onRequest(i2cRequestWrapper);
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
        Serial.println("Error: Maximum commands reached");
    }
}

void I2CCommandRouter::loop() {
    // Process any incoming commands
    processIncomingCommand();
    
    // Check for buffer overflow
    if (bufferOverflow) {
        Serial.println("I2C buffer overflow detected - messages lost");
        noInterrupts();
        bufferOverflow = false;
        lastStatus = STATUS_BUFFER_OVERFLOW;
        interrupts();
    }
}

void I2CCommandRouter::processIncomingCommand() {
    // Check if there's a request to process
    I2CRequest& currentRequest = i2cRequests[i2cRequestReadIndex];
    
    if (!currentRequest.inUse) {
        return;  // No request to process
    }
    
    // Process the request
    String command(currentRequest.message);
    command.trim();
    
    Serial.print("Processing I2C message (");
    Serial.print(currentRequest.bytesReceived);
    Serial.print(" bytes): ");
    Serial.println(command);
    
    if (command.length() > 0) {
        // Create a buffer stream for the response
        I2CCommandRouterBufferStream stream(responseBuffer, sizeof(responseBuffer));
        
        // Process the command
        processCommand(command, stream);
        
        // Store the response length and set status
        responseLength = stream.length;
        lastStatus = STATUS_OK;
    } else {
        // Empty command
        lastStatus = STATUS_INVALID_COMMAND;
        responseLength = 0;
    }
    
    // Clear the processed request
    currentRequest.inUse = false;
    currentRequest.bytesReceived = 0;
    memset(currentRequest.message, 0, sizeof(currentRequest.message));
    
    // Move to the next request
    i2cRequestReadIndex = (i2cRequestReadIndex + 1) % MAX_I2C_REQUESTS;
}

void I2CCommandRouter::processCommand(const String& rawCommand, OutputStream& output) {
    String cmd_str;
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
            Serial.print("With args: "); Serial.println(argsString);
            int argCount;
            String* args = parseArgs(argsString, argCount);
            
            // Call the handler with command name, args array, count, and output stream
            commands[i].handler(cmd_str, args, argCount, output);
            
            // Clean up
            if (argCount > 0 && args != nullptr) {
                delete[] args;
            }
            lastStatus = STATUS_OK;
            return;
        }
    }
    
    // Command not found
    Serial.print("Command not found: "); Serial.println(cmd_str);
    lastStatus = STATUS_COMMAND_NOT_FOUND;
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
    // Check if queue is full
    int nextWriteIndex = (i2cRequestWriteIndex + 1) % MAX_I2C_REQUESTS;
    if (nextWriteIndex == i2cRequestReadIndex) {
        // Queue is full - set overflow flag
        bufferOverflow = true;
        return;
    }
    
    I2CRequest& currentRequest = i2cRequests[i2cRequestWriteIndex];
    
    // Read the incoming data
    currentRequest.bytesReceived = min(bytes, I2C_BUFFER_SIZE - 1);
    for (int i = 0; i < currentRequest.bytesReceived; i++) {
        currentRequest.message[i] = Wire.read();
    }
    
    // Null terminate the message
    currentRequest.message[currentRequest.bytesReceived] = '\0';
    
    // Mark as in use BEFORE updating write index
    currentRequest.inUse = true;
    
    // Move to next slot for next message
    i2cRequestWriteIndex = nextWriteIndex;
}

void I2CCommandRouter::i2cReceiveWrapper(int bytes) {
    if (instance) {
        instance->handleI2CReceive(bytes);
    }
}

void I2CCommandRouter::i2cRequestWrapper() {
    if (instance) {
        instance->handleI2CRequest();
    }
}

void I2CCommandRouter::handleI2CRequest() {
    // Check if we have a response to send
    if (responseLength > 0) {
        // Send the response buffer
        Wire.write(responseBuffer, responseLength);
        
        // Add newline if not present
        if (responseLength < I2C_BUFFER_SIZE - 1 && 
            responseBuffer[responseLength - 1] != '\n') {
            Wire.write('\n');
        }
        
        // Add null terminator for debugging
        if (responseLength < I2C_BUFFER_SIZE) {
            responseBuffer[responseLength] = '\0';
        }
    } else {
        // No response data, send status
        switch (lastStatus) {
            case STATUS_OK:
                Wire.write("OK\n");
                break;
                
            case STATUS_COMMAND_NOT_FOUND:
                Wire.write("ERROR:CMD_NOT_FOUND\n");
                break;
                
            case STATUS_BUFFER_OVERFLOW:
                Wire.write("ERROR:BUFFER_OVERFLOW\n");
                break;
                
            case STATUS_INVALID_COMMAND:
                Wire.write("ERROR:INVALID_CMD\n");
                break;
                
            default:
                Wire.write("ERROR:UNKNOWN\n");
                break;
        }
    }
    
    // Reset for next command
    responseLength = 0;
    lastStatus = STATUS_OK;
}