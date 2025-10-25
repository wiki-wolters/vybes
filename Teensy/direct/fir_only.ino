#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

// NOTE: The build system must be configured to compile AudioFilterFIRFloat.cpp,
// which is located in the ../fir_filters/ directory. If using the Arduino IDE,
// you may need to copy AudioFilterFIRFloat.cpp and AudioFilterFIRFloat.h
// into this directory.
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

  AudioMemory(60);

  // Initialize the FIR filters with our coefficients
  firLeft.loadCoefficients(fir_coeffs, FIR_TAP_COUNT);
  firRight.loadCoefficients(fir_coeffs, FIR_TAP_COUNT);
  
  Serial.println("FIR filters initialized");
}

void loop() {
  // The audio library processes data automatically in the background.
  // Nothing to do here.
}
