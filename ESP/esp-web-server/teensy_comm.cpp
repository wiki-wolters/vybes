#include "globals.h"
#include "teensy_comm.h"
#include "i2c.h"
#include <Wire.h>

// I2C Configuration
#define TEENSY_I2C_ADDRESS 0x12  // Match this with the address in Teensy code
#define I2C_TIMEOUT 1000         // 1 second timeout for I2C operations

char teensyResponse[1024] = {0};  // Global buffer for storing the last response from Teensy
int responseIndex = 0;

bool sendToTeensy(const char* command, const String& param1, 
    const String& param2, const String& param3, const String& param4) {
    // Build the command string with parameters
    String message = String(command);

    if (param1.length() > 0) {
        message += ' ' + param1;

        if (param2.length() > 0) {
            message += ' ' + param2;

            if (param3.length() > 0) {
                message += ' ' + param3;

                if (param4.length() > 0) {
                    message += ' ' + param4;
                }
            }
        }
    }
    
    // Add newline to the message string instead of separate write
    message += '\n';

    // Debug output - show exact bytes
    Serial.print("Sending to Teensy (");
    Serial.print(message.length());
    Serial.print(" bytes): ");
    Serial.println(message);

    // Try to send the command up to 3 times
    for (int attempt = 0; attempt < 3; attempt++) {
        Wire.beginTransmission(TEENSY_I2C_ADDRESS);
        Wire.write(message.c_str(), message.length());
        byte error = Wire.endTransmission();

        if (error == 0) {
            Serial.println("I2C transmission successful");
            delay(10);
            break;
        }

        Serial.print("I2C transmission error (attempt ");
        Serial.print(attempt + 1);
        Serial.print("): ");
        Serial.println(error);

        if (attempt == 2) {
            Serial.println("Failed to communicate with Teensy after 3 attempts");
            return false;
        }

        delay(10);
    }

    // Rest of the function remains the same...
    // [Response reading code here]
    
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

void readFromTeensy() {
    //Reset teensyResponse
    memset(teensyResponse, 0, sizeof(teensyResponse));
    responseIndex = 0;

    Wire.requestFrom(TEENSY_I2C_ADDRESS, 1024);
    while (Wire.available()) {
        char c = Wire.read();
        teensyResponse[responseIndex] = c;
        responseIndex++;
    }
}

char* requestFromTeensy(const char* command) {
    sendToTeensy(command);
    readFromTeensy();

    return teensyResponse;
}

