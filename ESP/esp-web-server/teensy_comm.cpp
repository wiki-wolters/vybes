#include "globals.h"
#include "teensy_comm.h"
#include "i2c.h"
#include <Wire.h>

// I2C Configuration
#define TEENSY_I2C_ADDRESS 0x12  // Match this with the address in Teensy code
#define I2C_TIMEOUT 1000         // 1 second timeout for I2C operations
#define MAX_I2C_MESSAGE_LEN 256  // Define a max length for I2C messages

char teensyResponse[1024] = {0};  // Global buffer for storing the last response from Teensy
int responseIndex = 0;

// New, memory-efficient implementation with C-style strings
bool sendToTeensy(const char* command, const char* param1, const char* param2, const char* param3, const char* param4) {
    char message[MAX_I2C_MESSAGE_LEN];
    int offset = 0;

    // Build the command string safely
    offset += snprintf(message + offset, MAX_I2C_MESSAGE_LEN - offset, "%s", command);

    if (param1) {
        offset += snprintf(message + offset, MAX_I2C_MESSAGE_LEN - offset, " %s", param1);
    }
    if (param2) {
        offset += snprintf(message + offset, MAX_I2C_MESSAGE_LEN - offset, " %s", param2);
    }
    if (param3) {
        offset += snprintf(message + offset, MAX_I2C_MESSAGE_LEN - offset, " %s", param3);
    }
    if (param4) {
        offset += snprintf(message + offset, MAX_I2C_MESSAGE_LEN - offset, " %s", param4);
    }

    // Add newline
    snprintf(message + offset, MAX_I2C_MESSAGE_LEN - offset, "\n");

    // Debug output
    Serial.print("Sending to Teensy: ");
    Serial.print(message);

    // Try to send the command up to 5 times
    for (int attempt = 0; attempt < 5; attempt++) {
        Wire.beginTransmission(TEENSY_I2C_ADDRESS);
        Wire.write(message);
        byte error = Wire.endTransmission();

        if (error == 0) {
            // Serial.println("I2C transmission successful");
            return true; // Exit after success
        }

        Serial.print("I2C transmission error (attempt ");
        Serial.print(attempt + 1);
        Serial.print("): ");
        Serial.println(error);

        if (attempt == 4) {
            Serial.println("Failed to communicate with Teensy after 5 attempts");
            return false;
        }
        delay(10); // Short delay before retrying
    }
    return false; // Should not be reached
}


// Legacy implementation using String objects, now wraps the C-style string version
bool sendToTeensy(const char* command, const String& param1, 
    const String& param2, const String& param3, const String& param4) {
    return sendToTeensy(
        command,
        param1.length() > 0 ? param1.c_str() : nullptr,
        param2.length() > 0 ? param2.c_str() : nullptr,
        param3.length() > 0 ? param3.c_str() : nullptr,
        param4.length() > 0 ? param4.c_str() : nullptr
    );
}

// --- Helper functions updated to use C-style strings and be unambiguous ---

void sendOnOffToTeensy(const char* command, bool on) {
    sendToTeensy(command, on ? "1" : "0", nullptr);
}

void sendIntToTeensy(const char* command, int value) {
    char buffer[12]; // Buffer for integer conversion
    snprintf(buffer, sizeof(buffer), "%d", value);
    sendToTeensy(command, buffer, nullptr);
}

void sendFloatToTeensy(const char* command, float value) {
    char buffer[32]; // Buffer for float conversion
    dtostrf(value, 1, 2, buffer); // 1 minimum width, 2 decimal places
    sendToTeensy(command, buffer, nullptr);
}

// Overload for const char*
void sendStringToTeensy(const char* command, const char* value) {
    sendToTeensy(command, value, nullptr);
}

// Overload for String for backward compatibility
void sendStringToTeensy(const char* command, const String& value) {
    sendToTeensy(command, value.c_str(), nullptr);
}


void readFromTeensy() {
    //Reset teensyResponse
    memset(teensyResponse, 0, sizeof(teensyResponse));
    responseIndex = 0;

    Wire.requestFrom(TEENSY_I2C_ADDRESS, 1024);
    while (Wire.available()) {
        char c = Wire.read();
        if (responseIndex < sizeof(teensyResponse) - 1) {
            teensyResponse[responseIndex++] = c;
        }
    }
    teensyResponse[responseIndex] = '\0'; // Null-terminate
}

char* requestFromTeensy(const char* command) {
    if (sendToTeensy(command, nullptr)) {
        readFromTeensy();
    }
    return teensyResponse;
}
