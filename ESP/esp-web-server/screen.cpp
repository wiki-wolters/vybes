#include "screen.h"
#include <Wire.h>
#include <Arduino.h>
#include <LiquidCrystal_PCF8574.h>

// Screen is 1602 LCD via I2C with PCF8574 backpack
LiquidCrystal_PCF8574 lcd(0x27); // Default I2C address 0x27

const long MAX_BACKLIGHT_MILLIS = 5000;

unsigned long messageStart = 0;
unsigned long messageDuration = 0;
unsigned long backlightStart = 0;
String currentMessage = "";

void setupScreen() {    
    // Try to initialize the LCD
    lcd.begin(16, 2);  // Initialize for 16x2 display
    lcd.setBacklight(0);  // Turn off backlight initially
    lcd.clear();
    
    // Display a test message
    lcd.setCursor(0, 0);
    lcd.print("Vybes starting"); // 16x2 display: keep within 16 chars
    
    // Store empty string as current message
    currentMessage = "";
}

void writeToScreen(String message, unsigned long duration) {
    lcd.clear();
    lcd.home();
    
    // Split message into two lines if it contains a newline
    int newlinePos = message.indexOf('\n');
    if (newlinePos != -1) {
        lcd.print(message.substring(0, newlinePos));
        lcd.setCursor(0, 1);
        lcd.print(message.substring(newlinePos + 1));
    } else {
        lcd.print(message);
    }
    
    if (duration > 0) {
        messageStart = millis();
        messageDuration = duration;
    } else {
        currentMessage = message;
    }
    lcd.setBacklight(1);
    backlightStart = millis();
}

// The backlight timer starts inside setup(), but loopScreen() - the only
// thing that expires it - cannot run until setup() returns. WiFi association
// alone was measured at 1.2-5.2s across boots, so most of the 5s budget was
// spent before the display was ever worth looking at, and a slow association
// meant the first loopScreen() call blanked the backlight the instant the
// device came up (measured: 4.93s of the 5s gone on a 5.2s association).
// Restart the clock once boot is done so the 5s is 5s of visible time.
void restartBacklightTimer() {
    if (backlightStart > 0) {
        backlightStart = millis();
    }
}

void loopScreen() {
    // This function handles timed messages
    if (messageDuration > 0 && (millis() - messageStart) > messageDuration) {
        // Only clear and rewrite if we have a current message
        if (currentMessage.length() > 0) {
            writeToScreen(currentMessage);
        }
        messageDuration = 0;
    }

    if (backlightStart > 0 && millis() - backlightStart > MAX_BACKLIGHT_MILLIS) {
        lcd.setBacklight(0);
        backlightStart = 0;
    }
}
