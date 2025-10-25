#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include "AudioFilterFIRFloat.h"

// GUItool: begin automatically generated code
AudioInputUSB            usb1;
AudioFilterFIRFloat      firLeft;
AudioFilterFIRFloat      firRight;
AudioOutputI2S           i2s1;
AudioConnection          patchCord1(usb1, 0, firLeft, 0);
AudioConnection          patchCord2(firLeft, 0, i2s1, 0);
AudioConnection          patchCord3(usb1, 1, firRight, 0);
AudioConnection          patchCord4(firRight, 0, i2s1, 1);
AudioControlSGTL5000     sgtl5000_1;
// GUItool: end automatically generated code

// A simple 5-tap low-pass FIR filter
const int FIR_TAP_COUNT = 5;
float fir_coeffs[FIR_TAP_COUNT] = {0.1, 0.2, 0.4, 0.2, 0.1};

void setup() {
  Serial.begin(9600);
  while (!Serial && millis() < 4000) {
    // wait for serial port to connect.
  }
  Serial.println("Starting FIR filter test");
  Serial.println("Send '1' to enable FIR filter, '0' to disable.");

  AudioMemory(60);

  // Enable the audio shield and set the output volume.
  sgtl5000_1.enable();
  sgtl5000_1.volume(0.6);

  // Initialize the FIR filters with our coefficients
  firLeft.loadCoefficients(fir_coeffs, FIR_TAP_COUNT);
  firRight.loadCoefficients(fir_coeffs, FIR_TAP_COUNT);
  
  Serial.println("FIR filters initialized and ENABLED by default.");
}

void loop() {
  // Check for serial commands
  if (Serial.available() > 0) {
    char command = Serial.read();
    if (command == '0') {
      firLeft.setEnabled(false);
      firRight.setEnabled(false);
      Serial.println("FIR filter DISABLED");
    } else if (command == '1') {
      firLeft.setEnabled(true);
      firRight.setEnabled(true);
      Serial.println("FIR filter ENABLED");
    }
  }
}