#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

// GUItool: begin automatically generated code
AsyncAudioInputSPDIF3    Optical_in;   //xy=173,346
AudioOutputI2S           L_R_Analog_Out;           //xy=545,347
AudioConnection          patchCord1(Optical_in, 0, L_R_Analog_Out, 0);
AudioConnection          patchCord2(Optical_in, 1, L_R_Analog_Out, 1);
// GUItool: end automatically generated code

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
    // Wait for serial connection or timeout after 3 seconds
  }
  
  // Audio connections require memory to work
  AudioMemory(20);
  
  Serial.println("SPDIF Optical Input to Analog Output Test");
  Serial.println("Connect optical input cable and analog output (headphones/speakers)");
}

void loop() {
  // Optional: Print some diagnostics every 2 seconds
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 2000) {
    lastPrint = millis();
    
    // Check if we're getting input signal
    if (Optical_in.isLocked()) {
      Serial.println("SPDIF Input Locked - Signal detected");
    } else {
      Serial.println("No SPDIF input signal detected");
    }
    
    // Print memory usage
    Serial.print("Audio Memory Usage: ");
    Serial.print(AudioMemoryUsage());
    Serial.print(" / ");
    Serial.println(AudioMemoryUsageMax());
  }
}