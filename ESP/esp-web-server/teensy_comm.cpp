#include "globals.h"
#include "teensy_comm.h"
#include <Wire.h>

// I2C Configuration
#define TEENSY_I2C_ADDRESS 0x08  // Match this with the address in Teensy code
#define I2C_SDA 4                // GPIO4 (D2)
#define I2C_SCL 5                // GPIO5 (D1)
#define I2C_TIMEOUT 1000         // 1 second timeout for I2C operations

bool i2cInitialized = false;

void initI2C() {
    if (!i2cInitialized) {
        Wire.begin(I2C_SDA, I2C_SCL);
        Wire.setClock(100000);  // Standard 100kHz I2C
        i2cInitialized = true;
    }
}

String sendToTeensy(const String& command, const String& data) {
    initI2C();
    
    // Combine command and data with space delimiter
    String message = command;
    if (data.length() > 0) {
        message += ' ' + data;
    }
    
    // Send command to Teensy
    Wire.beginTransmission(TEENSY_I2C_ADDRESS);
    Wire.write(message.c_str(), message.length());
    Wire.endTransmission();
    
    // Small delay to allow Teensy to process the command
    delay(10);
    
    // Request response from Teensy
    uint8_t response[256] = {0};
    uint8_t responseLength = 0;
    
    // Request data from Teensy
    Wire.requestFrom(TEENSY_I2C_ADDRESS, (uint8_t)sizeof(response) - 1);
    
    // Read response with timeout
    uint32_t startTime = millis();
    while (Wire.available() == 0 && (millis() - startTime) < I2C_TIMEOUT) {
        delay(1);
    }
    
    // Read available bytes
    while (Wire.available() > 0 && responseLength < sizeof(response) - 1) {
        response[responseLength++] = Wire.read();
    }
    
    // Null-terminate the response
    response[responseLength] = '\0';
    
    // Convert to String and trim any whitespace
    String responseStr = String((char*)response);
    responseStr.trim();
    
    // Debug output
    Serial.print("Sent to Teensy: ");
    Serial.println(message);
    Serial.print("Received from Teensy: \"");
    Serial.print(responseStr);
    Serial.println("\"");
    
    return responseStr;
}
