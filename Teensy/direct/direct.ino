#include <Audio.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

// GUItool: begin automatically generated code
AudioAnalyzePeak peak1;
//AudioInputI2S    Audio_in;
//AsyncAudioInputSPDIF3    Audio_in;
AudioInputUSB    Audio_in;
AudioOutputI2S           L_R_Analog_Out;
AudioConnection          patchCord1(Audio_in, 0, L_R_Analog_Out, 0);
AudioConnection          patchCord2(Audio_in, 1, L_R_Analog_Out, 1);
AudioConnection          patchCord3(Audio_in, 0, peak1, 0);
// GUItool: end automatically generated code

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
    // Wait for serial connection or timeout after 3 seconds
  }
  
  // Audio connections require memory to work
  AudioMemory(20);
  
  Serial.println("I2S Input to Analog Output Test with Peak Meter");
}

void loop() {
  // Optional: Print some diagnostics every 2 seconds
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 2000) {
    lastPrint = millis();
    
    if (peak1.available()) {
      Serial.print("Peak Level: ");
      Serial.println(peak1.read());
    }
    
    // Print memory usage
    Serial.print("Audio Memory Usage: ");
    Serial.print(AudioMemoryUsage());
    Serial.print(" / ");
    Serial.println(AudioMemoryUsageMax());
  }
}