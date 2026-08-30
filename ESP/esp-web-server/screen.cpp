#include "screen.h"
#include <Wire.h>
#include <Arduino.h>
#include <LiquidCrystal_PCF8574.h>
#include "globals.h"
#include "config.h"
#include "teensy_comm.h"

// Screen is 1602 LCD via I2C with PCF8574 backpack
static const uint8_t LCD_I2C_ADDR = 0x27; // Default PCF8574 address
LiquidCrystal_PCF8574 lcd(LCD_I2C_ADDR);

const long MAX_BACKLIGHT_MILLIS = 5000;

// How long boot will wait for the backpack to ACK before giving up on it
// (loopScreen keeps probing after that, so a late screen still comes up).
const unsigned long SCREEN_BOOT_WAIT_MS = 1000;
const unsigned long SCREEN_PROBE_INTERVAL_MS = 2000;

// The PCF8574 ACKs from ~2.5V, but the HD44780 behind it needs its own 5V
// power-on reset to finish before it will accept instructions. An ACK is
// therefore not proof the display is ready: always let the rail settle
// before the init sequence, however early the backpack started answering.
const unsigned long LCD_SETTLE_MS = 100;

unsigned long messageStart = 0;
unsigned long messageDuration = 0;
unsigned long backlightStart = 0;
String currentMessage = "";

static bool screenPresent = false;
static unsigned long lastProbeMillis = 0;

// Custom glyph slot for the recording dot ("\x01" in messages; slot 0 would
// terminate the String).
#define REC_DOT_CHAR 1

// begin() checks no ACKs, so a display that isn't powered and settled when
// this runs ignores the sequence and stays dark while every later print()
// "succeeds". Nothing recovers that on its own - hence probeScreenPresence(),
// which calls this again the moment the backpack reappears. Re-running it is
// safe: the HD44780's "Initializing by Instruction" sequence is idempotent.
//
// begin() is also the only thing that binds the library's TwoWire pointer -
// the constructor leaves it null - so this has to run at boot even when
// nothing answered. See setupScreen().
static void initLcd() {
    lcd.begin(16, 2);  // Initialize for 16x2 display
    lcd.setBacklight(0);  // Turn off backlight initially
    lcd.clear();

    // Recording dot glyph
    byte recDot[8] = {0b00000, 0b01110, 0b11111, 0b11111,
                      0b11111, 0b01110, 0b00000, 0b00000};
    lcd.createChar(REC_DOT_CHAR, recDot);
}

// The library binds its TwoWire pointer inside begin() and nowhere else -
// the constructor leaves it null - so lcd.isConnected() faults on a null
// port until initLcd() has run. That is precisely the probe this code has to
// make *before* init, so probe the bus directly instead.
// Wire is already up: initI2C() runs ahead of setupScreen().
static bool backpackAcks() {
    Wire.beginTransmission(LCD_I2C_ADDR);
    return Wire.endTransmission() == 0;
}

void setupScreen() {
    // The screen's 5V rail can come up after the ESP does (separate supply,
    // slow ramp), and a blind lcd.begin() into an unpowered backpack is what
    // used to leave the screen blank for the whole session. Wait briefly for
    // the backpack to ACK; a screen that misses the window is picked up by
    // the probe in loopScreen() whenever it appears.
    screenPresent = backpackAcks();
    if (!screenPresent) {
        unsigned long waitStart = millis();
        while (!screenPresent && millis() - waitStart < SCREEN_BOOT_WAIT_MS) {
            delay(50);
            screenPresent = backpackAcks();
        }
    }

    if (screenPresent) {
        delay(LCD_SETTLE_MS);
    } else {
        DebugSerial.println("LCD not responding; will keep probing");
    }

    // Initialize either way. Returning early here left every lcd.* call with
    // a null TwoWire pointer, and setup()'s very next line - writeToScreen()
    // - faulted on it: the ESP panicked and rebooted a second into every
    // boot, long before WiFi, so a screen that was merely late took the whole
    // device into a boot loop and the probe below never got to run. Into an
    // absent backpack the transfers just NAK and cost a few ms.
    initLcd();

    // Display a test message
    lcd.setCursor(0, 0);
    lcd.print("Vybes starting"); // 16x2 display: keep within 16 chars

    // Store empty string as current message
    currentMessage = "";
}

// Periodic presence probe. A screen that was absent (or lost power) gets the
// full init sequence again the moment it ACKs, then the persistent message
// is rewritten - so the display self-heals no matter which rail came up
// first. Timed messages are dropped on re-init: whatever they said predates
// the outage.
static void probeScreenPresence() {
    if (millis() - lastProbeMillis < SCREEN_PROBE_INTERVAL_MS) {
        return;
    }
    lastProbeMillis = millis();

    bool present = backpackAcks();
    if (present && !screenPresent) {
        DebugSerial.println("LCD appeared; initializing");
        delay(LCD_SETTLE_MS); // the rail just came up; let it settle
        initLcd();
        screenPresent = true;
        messageDuration = 0;
        if (currentMessage.length() > 0) {
            writeToScreen(currentMessage);
        }
    } else if (!present && screenPresent) {
        DebugSerial.println("LCD stopped responding");
        screenPresent = false;
    }
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
    probeScreenPresence();
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
