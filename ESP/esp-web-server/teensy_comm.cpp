#include "globals.h"
#include "teensy_comm.h"

void sendToTeensy(const String& command, const String& data) {
    // TODO: Implement actual I2C communication
    Serial.println("TODO: Send to Teensy - " + command + ": " + data);
}
