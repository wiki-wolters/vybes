#include "button.h"
#include "config.h"
#include "screen.h"
#include <Wire.h>
#include <string.h>

const int BUTTON_PIN = 13; //D7
const int BLUETOOTH_PAIRING_PIN = 12; //D6, for output

unsigned long pressStart = 0;
String lastMessage = "";

void setupButton() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(BLUETOOTH_PAIRING_PIN, OUTPUT);
    digitalWrite(BLUETOOTH_PAIRING_PIN, LOW);
}

void handleShortPress() {
    nextPreset();

    // Serial.println("Scanning I2C bus...");
    // byte error, address;
    // int nDevices = 0;
    // for(address = 1; address < 127; address++ ) {
    //     Wire.beginTransmission(address);
    //     error = Wire.endTransmission();
    //     if (error == 0) {
    //         Serial.print("I2C device found at 0x");
    //         if (address<16) Serial.print("0");
    //         Serial.println(address,HEX);
    //         nDevices++;
    //     }
    // }
    // if (nDevices == 0) {
    //     Serial.println("No I2C devices found");
    // }
}

void handleButton() {
    if (digitalRead(BUTTON_PIN) == LOW) {
        if (pressStart == 0) {
            Serial.println("Button pressed down");
            pressStart = millis();
        }

        //If elapsed time is longer than 500ms, it's a long press
        if (millis() - pressStart > 3600) {
            if (lastMessage != "pairing") {
                writeToScreen("Pairing mode...", 2000);
                lastMessage = "pairing";
            }
        } else if (millis() - pressStart > 500) {
            if (lastMessage != "hold") {
                digitalWrite(BLUETOOTH_PAIRING_PIN, HIGH);
                writeToScreen("Hold for 3s to pair");
                lastMessage = "hold";
            }
        } 
    } else {
        if (pressStart > 0) {
            if (millis() - pressStart <= 500) {
                Serial.println("Button short pressed");
                handleShortPress();
            }

            digitalWrite(BLUETOOTH_PAIRING_PIN, LOW);
            pressStart = 0;
        }
    }
}