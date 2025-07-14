#include "button.h"
#include "config.h"
#include "screen.h"
#include <Wire.h>
#include <string.h>

const int BUTTON_PIN = 13; //D7
const int BLUETOOTH_PAIRING_PIN = 12; //D6, for output

unsigned long pressStart = 0;
String lastMessage = "";
unsigned long lastButtonPressTime = 0;
int8_t currentPresetIndex = 0;

// Add function to check if backlight is on
extern unsigned long backlightStart; // Need to access this from screen.cpp
extern LiquidCrystal_PCF8574 lcd;    // Need to access LCD object

bool isBacklightOn() {
    return backlightStart > 0;
}

void setupButton() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(BLUETOOTH_PAIRING_PIN, OUTPUT);
    digitalWrite(BLUETOOTH_PAIRING_PIN, LOW);
    currentPresetIndex = current_config.active_preset_index;
}

void nextPreset() {
    currentPresetIndex++;
    if (currentPresetIndex >= MAX_PRESETS || strlen(current_config.presets[currentPresetIndex].name) == 0) {
        currentPresetIndex = 0;
    }
    writeToScreen(current_config.presets[currentPresetIndex].name);
    lastButtonPressTime = millis();
}

void handleShortPress() {
    // Check if backlight is off
    if (!isBacklightOn()) {
        // Just turn on backlight and show current preset
        lcd.setBacklight(1);
        backlightStart = millis();
        writeToScreen(current_config.presets[currentPresetIndex].name);
    } else {
        // Backlight is on, so cycle to next preset
        nextPreset();
    }
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

    if (lastButtonPressTime > 0 && millis() - lastButtonPressTime > 1000) {
        if (currentPresetIndex != current_config.active_preset_index) {
            current_config.active_preset_index = currentPresetIndex;
            updateTeensyWithActivePresetParameters();
            save_config();
        }
        lastButtonPressTime = 0;
    }
}