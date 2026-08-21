#include "screen.h"
#include <Wire.h>
#include <Arduino.h>
#include <LiquidCrystal_PCF8574.h>
#include "config.h"
#include "teensy_comm.h"

// Screen is 1602 LCD via I2C with PCF8574 backpack
LiquidCrystal_PCF8574 lcd(0x27); // Default I2C address 0x27

const long MAX_BACKLIGHT_MILLIS = 5000;

unsigned long messageStart = 0;
unsigned long messageDuration = 0;
unsigned long backlightStart = 0;
String currentMessage = "";

// Custom glyph slot for the recording dot ("\x01" in messages; slot 0 would
// terminate the String).
#define REC_DOT_CHAR 1

void setupScreen() {
    // Try to initialize the LCD
    lcd.begin(16, 2);  // Initialize for 16x2 display
    lcd.setBacklight(0);  // Turn off backlight initially
    lcd.clear();

    // Recording dot glyph
    byte recDot[8] = {0b00000, 0b01110, 0b11111, 0b11111,
                      0b11111, 0b01110, 0b00000, 0b00000};
    lcd.createChar(REC_DOT_CHAR, recDot);

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

// While a recording runs, hold "<dot> REC mm:ss" + filename as the
// persistent message. Rewriting it on each elapsed-seconds tick also feeds
// the backlight timer, so the display stays lit for the whole recording;
// timed messages (volume changes, lock notices) still overlay it and fall
// back to it when they expire.
static void updateRecordingMessage() {
    static bool wasRecording = false;
    static uint32_t lastShownSeconds = UINT32_MAX;

    RecorderState rs;
    getRecorderState(rs);

    if (rs.recording) {
        wasRecording = true;
        if (messageDuration == 0 && rs.recordSeconds != lastShownSeconds) {
            lastShownSeconds = rs.recordSeconds;
            char line[40];
            uint32_t mins = rs.recordSeconds / 60;
            uint32_t secs = rs.recordSeconds % 60;
            snprintf(line, sizeof(line), "\x01 REC %lu:%02lu\n%s",
                     (unsigned long)mins, (unsigned long)secs, rs.recordFile);
            writeToScreen(String(line));
        }
    } else if (wasRecording) {
        // Recording ended: fall back to the active preset name, then let the
        // normal backlight timeout run its course
        wasRecording = false;
        lastShownSeconds = UINT32_MAX;
        writeToScreen(current_config.presets[current_config.active_preset_index].name);
    }
}

void loopScreen() {
    updateRecordingMessage();

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
