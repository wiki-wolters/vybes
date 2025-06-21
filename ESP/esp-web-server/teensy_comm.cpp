#include "globals.h"
#include "teensy_comm.h"
#include <Wire.h>

// I2C Configuration
#define TEENSY_I2C_ADDRESS 0x08  // Match this with the address in Teensy code
#define I2C_SDA 4                // GPIO4 (D2)
#define I2C_SCL 5                // GPIO5 (D1)
#define I2C_TIMEOUT 1000         // 1 second timeout for I2C operations

bool i2cInitialized = false;
char teensyResponse[256] = {0};  // Global buffer for storing the last response from Teensy

void initI2C() {
    if (!i2cInitialized) {
        Wire.begin(I2C_SDA, I2C_SCL);
        Wire.setClock(100000);  // Standard 100kHz I2C
        i2cInitialized = true;
    }
}

bool sendToTeensy(const char* command, const String& param1, 
                 const String& param2, const String& param3) {
    initI2C();
    
    // Build the command string with parameters
    String message = String(command);
    
    if (param1.length() > 0) {
        message += ' ' + param1;
        
        if (param2.length() > 0) {
            message += ' ' + param2;
            
            if (param3.length() > 0) {
                message += ' ' + param3;
            }
        }
    }
    
    // Debug output
    Serial.print("Sending to Teensy: ");
    Serial.println(message);
    
    // Send command to Teensy
    Wire.beginTransmission(TEENSY_I2C_ADDRESS);
    Wire.write(message.c_str(), message.length());
    byte error = Wire.endTransmission();
    
    if (error != 0) {
        Serial.print("I2C transmission error: ");
        Serial.println(error);
        return false;
    }
    
    // Small delay to allow Teensy to process the command
    delay(2);
    
    // Request response from Teensy
    uint8_t response[256] = {0};
    uint8_t responseLength = 0;
    
    // Request data from Teensy with error handling
    Wire.requestFrom(TEENSY_I2C_ADDRESS, (uint8_t)sizeof(response) - 1);
    
    // Read response with timeout
    uint32_t startTime = millis();
    while (Wire.available() == 0 && (millis() - startTime) < I2C_TIMEOUT) {
        delay(1);
    }
    
    // Read available bytes
    while (Wire.available() > 0 && responseLength < sizeof(teensyResponse) - 1) {
        char c = Wire.read();
        response[responseLength++] = c;
        teensyResponse[responseLength-1] = c;  // Store in global buffer
    }
    
    // Null-terminate the responses
    response[responseLength] = '\0';
    teensyResponse[responseLength] = '\0';
    
    // Debug output
    if (responseLength > 0) {
        Serial.print("Received from Teensy: \"");
        Serial.print((char*)response);
        Serial.println("\"");
        
        // Also copy to the global buffer
        strncpy(teensyResponse, (char*)response, sizeof(teensyResponse) - 1);
        teensyResponse[sizeof(teensyResponse) - 1] = '\0'; // Ensure null termination
    } else {
        Serial.println("No response from Teensy");
        teensyResponse[0] = '\0';  // Clear the response buffer
    }
    
    return true;
}

// Helper function to send a simple on/off command to Teensy
void sendOnOffToTeensy(const char* command, bool on) {
    sendToTeensy(command, on ? "1" : "0");  // Send "1"/"0" instead of "on"/"off"
}

// Helper function to send a command with a single integer parameter to Teensy
void sendIntToTeensy(const char* command, int value) {
    sendToTeensy(command, String(value));
}

// Helper function to send a command with a single float parameter to Teensy
void sendFloatToTeensy(const char* command, float value) {
    // Format float with 2 decimal places
    char buffer[32];
    dtostrf(value, 1, 2, buffer);
    sendToTeensy(command, buffer);
}

// Helper function to send a command with a string parameter to Teensy
void sendStringToTeensy(const char* command, const String& value) {
    sendToTeensy(command, value);
}
