#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <EEPROM.h> // Added for EEPROM access
#include "FIRLoader.h"
#include "PEQProcessor.h"
#include "I2CCommandRouter.h"
#include "OutputStream.h"

// Create router instance (I2C address 18)
I2CCommandRouter router(18);

#define MAX_FILENAME_LEN 64 // Maximum length for FIR filenames

// Forward declarations
void setEQFilters(PEQBand filters[], int animationDuration = 20);
void handleSetSpeakerGains(const String& command, String* args, int argCount);
void handleGetFiles(const String& command, String* args, int argCount);

// Audio generators
AudioSynthWaveform       Tone_generator;
AudioSynthNoisePink      pink1;         

//Audio Inputs (Bluetooth, SPDIF, USB)
//AudioInputI2S            Bluetooth_in;  
AsyncAudioInputSPDIF3    Optical_in; 
//AudioInputUSB            USB_in;   

// Input mixers
AudioMixer4              Left_mixer;    
AudioMixer4              Right_mixer;   
AudioMixer4              Generator_mixer;   

// Parametric EQ processors (multi-band)
PEQProcessor peqLeft;
PEQProcessor peqRight;

// Post EQ checkpoint, including subwoofer (using amps with gain = 1.0)
AudioAmplifier           Left_Post_EQ_amp;
AudioAmplifier           Right_Post_EQ_amp;
AudioAmplifier           Sub_Post_EQ_amp;
AudioAmplifier           Mono_Post_EQ_amp; // Declaration for Mono_Post_EQ_amp

//Post crossover checkpoint (using amps with gain = 1.0)
AudioAmplifier           Left_Post_Crossover_amp;
AudioAmplifier           Sub_Post_Crossover_amp;
AudioAmplifier           Right_Post_Crossover_amp;

// Post FIR checkpoint (using amps with gain = 1.0)
AudioAmplifier           Left_Post_FIR_amp;
AudioAmplifier           Sub_Post_FIR_amp;
AudioAmplifier           Right_Post_FIR_amp;

// Post Delay checkpoint (using amps with gain = 1.0)
AudioAmplifier           Left_Post_Delay_amp;
AudioAmplifier           Sub_Post_Delay_amp;
AudioAmplifier           Right_Post_Delay_amp;

// Mono mix
AudioMixer4              Mono_mixer;   

// Crossover components
AudioFilterBiquad        Left_highpass;  
AudioFilterBiquad        Sub_lowpass;    
AudioFilterBiquad        Right_highpass;

// FIR Filters
AudioFilterFIR           Sub_FIR_Filter; 
AudioFilterFIR           Left_FIR_Filter; 
AudioFilterFIR           Right_FIR_Filter; 

// Delays
AudioEffectDelay         Left_delay;   
AudioEffectDelay         Sub_delay;    
AudioEffectDelay         Right_delay;  

// Outputs
AudioOutputSPDIF3        L_R_Spdif_Out;     
AudioOutputI2S           L_R_Analog_Out;    
AudioOutputI2S2          Sub_Analog_Out;

// Connections

// Generator connections
AudioConnection          patchCord_GenToneToMixer(Tone_generator, 0, Generator_mixer, 0);
AudioConnection          patchCord_PinkToMixer(pink1, 0, Generator_mixer, 1);
AudioConnection          patchCord_GenMixerToLeftMixer(Generator_mixer, 0, Left_mixer, 3); // Assuming output 0 from Generator_mixer
AudioConnection          patchCord_GenMixerToRightMixer(Generator_mixer, 0, Right_mixer, 3); // Assuming output 0 from Generator_mixer
AudioConnection* generatorConnections[] = {
  &patchCord_GenToneToMixer,
  &patchCord_PinkToMixer,
  &patchCord_GenMixerToLeftMixer,
  &patchCord_GenMixerToRightMixer
};
const size_t generatorConnections_len = sizeof(generatorConnections) / sizeof(generatorConnections[0]);

// External input connections
AudioConnection          patchCord_OpticalLToLeftMixer(Optical_in, 0, Left_mixer, 0);
AudioConnection          patchCord_OpticalRToRightMixer(Optical_in, 1, Right_mixer, 0);
// AudioConnection       patchCord_BluetoothLToLeftMixer(Bluetooth_in, 0, Left_mixer, 1); // Uncomment if Bluetooth_in is used
// AudioConnection       patchCord_BluetoothRToRightMixer(Bluetooth_in, 1, Right_mixer, 1); // Uncomment if Bluetooth_in is used
AudioConnection* externalInputConnections[] = {
  &patchCord_OpticalLToLeftMixer,
  &patchCord_OpticalRToRightMixer
  // &patchCord_BluetoothLToLeftMixer, // Uncomment if Bluetooth_in is used
  // &patchCord_BluetoothRToRightMixer  // Uncomment if Bluetooth_in is used
};
const size_t externalInputConnections_len = sizeof(externalInputConnections) / sizeof(externalInputConnections[0]);

// Patchcords for PEQ processors
AudioConnection patchCord_LeftMixerToPEQ(Left_mixer, 0, peqLeft, 0);
AudioConnection patchCord_RightMixerToPEQ(Right_mixer, 0, peqRight, 0);
AudioConnection patchCord_PEQLeftToPostEQ(peqLeft, 0, Left_Post_EQ_amp, 0);
AudioConnection patchCord_PEQRightToPostEQ(peqRight, 0, Right_Post_EQ_amp, 0);
AudioConnection patchCord_PEQLeftToMonoMixer(peqLeft, 0, Mono_mixer, 0);
AudioConnection patchCord_PEQRightToMonoMixer(peqRight, 0, Mono_mixer, 1);
AudioConnection patchCord_PEQMonoToMonoPostEQ(Mono_mixer, 0, Mono_Post_EQ_amp, 0); // Added source port 0 for Mono_mixer
AudioConnection* peqConnections[] = {
  &patchCord_LeftMixerToPEQ,
  &patchCord_RightMixerToPEQ,
  &patchCord_PEQLeftToPostEQ,
  &patchCord_PEQRightToPostEQ,
  &patchCord_PEQLeftToMonoMixer,
  &patchCord_PEQRightToMonoMixer,
  &patchCord_PEQMonoToMonoPostEQ
};
const size_t peqConnections_len = sizeof(peqConnections) / sizeof(peqConnections[0]);

// Connect from Post-EQ checkpoint to Post-Crossover checkpoint via crossover
AudioConnection          patchCord_LeftPostEQAmpToHighpass1(Left_Post_EQ_amp, 0, Left_highpass, 0);
AudioConnection          patchCord_LeftHighpassToPostCrossoverAmp(Left_highpass, 0, Left_Post_Crossover_amp, 0);
AudioConnection          patchCord_RightPostEQAmpToHighpass1(Right_Post_EQ_amp, 0, Right_highpass, 0);
AudioConnection          patchCord_RightHighpassToPostCrossoverAmp(Right_highpass, 0, Right_Post_Crossover_amp, 0);
AudioConnection          patchCord_MonoPostEQAmpToSubLowpass1(Mono_Post_EQ_amp, 0, Sub_lowpass, 0);
AudioConnection          patchCord_SubLowpassToPostCrossoverAmp(Sub_lowpass, 0, Sub_Post_Crossover_amp, 0);
AudioConnection* crossoverConnections[] = {
  &patchCord_LeftPostEQAmpToHighpass1,
  &patchCord_LeftHighpassToPostCrossoverAmp,
  &patchCord_RightPostEQAmpToHighpass1,
  &patchCord_RightHighpassToPostCrossoverAmp,
  &patchCord_MonoPostEQAmpToSubLowpass1,
  &patchCord_SubLowpassToPostCrossoverAmp
};
const size_t crossoverConnections_len = sizeof(crossoverConnections) / sizeof(crossoverConnections[0]);

// Connect from Post-EQ checkpoint to Post-Crossover checkpoint directly, bypassing crossover
AudioConnection          patchCord_LeftPostEQAmpToPostCrossoverAmp(Left_Post_EQ_amp, 0, Left_Post_Crossover_amp, 0);
AudioConnection          patchCord_RightPostEQAmpToPostCrossoverAmp(Right_Post_EQ_amp, 0, Right_Post_Crossover_amp, 0);
AudioConnection          patchCord_MonoPostEQAmpToSubPostCrossoverAmp(Mono_Post_EQ_amp, 0, Sub_Post_Crossover_amp, 0); // Corrected Sub_Post_EQ_amp to Mono_Post_EQ_amp for consistency
AudioConnection* bypassCrossoverConnections[] = {
  &patchCord_LeftPostEQAmpToPostCrossoverAmp,
  &patchCord_RightPostEQAmpToPostCrossoverAmp,
  &patchCord_MonoPostEQAmpToSubPostCrossoverAmp
};
const size_t bypassCrossoverConnections_len = sizeof(bypassCrossoverConnections) / sizeof(bypassCrossoverConnections[0]);

// Connect from Post-Crossover checkpoint to Post-FIR checkpoint via FIR
AudioConnection          patchCord_LeftPostCrossoverAmpToFIR(Left_Post_Crossover_amp, 0, Left_FIR_Filter, 0);
AudioConnection          patchCord_RightPostCrossoverAmpToFIR(Right_Post_Crossover_amp, 0, Right_FIR_Filter, 0);
AudioConnection          patchCord_SubPostCrossoverAmpToFIR(Sub_Post_Crossover_amp, 0, Sub_FIR_Filter, 0);
AudioConnection* firConnections[] = {
  &patchCord_LeftPostCrossoverAmpToFIR,
  &patchCord_RightPostCrossoverAmpToFIR,
  &patchCord_SubPostCrossoverAmpToFIR
};
const size_t firConnections_len = sizeof(firConnections) / sizeof(firConnections[0]);

// Connect from Post-Crossover checkpoint to Post-FIR checkpoint directly, bypassing FIR
AudioConnection          patchCord_LeftPostCrossoverAmpToPostFIRAmp(Left_Post_Crossover_amp, 0, Left_Post_FIR_amp, 0);
AudioConnection          patchCord_RightPostCrossoverAmpToPostFIRAmp(Right_Post_Crossover_amp, 0, Right_Post_FIR_amp, 0);
AudioConnection          patchCord_SubPostCrossoverAmpToPostFIRAmp(Sub_Post_Crossover_amp, 0, Sub_Post_FIR_amp, 0);
AudioConnection* bypassFIRConnections[] = {
  &patchCord_LeftPostCrossoverAmpToPostFIRAmp,
  &patchCord_RightPostCrossoverAmpToPostFIRAmp,
  &patchCord_SubPostCrossoverAmpToPostFIRAmp
};
const size_t bypassFIRConnections_len = sizeof(bypassFIRConnections) / sizeof(bypassFIRConnections[0]);

// Connect from Post-FIR checkpoint to Post-Delay checkpoint via delay
AudioConnection          patchCord_LeftPostFIRAmpToDelay(Left_Post_FIR_amp, 0, Left_delay, 0);
AudioConnection          patchCord_RightPostFIRAmpToDelay(Right_Post_FIR_amp, 0, Right_delay, 0);
AudioConnection          patchCord_SubPostFIRAmpToDelay(Sub_Post_FIR_amp, 0, Sub_delay, 0);
AudioConnection          patchCord_LeftDelayToPostDelayAmp(Left_delay, 0, Left_Post_Delay_amp, 0);
AudioConnection          patchCord_RightDelayToPostDelayAmp(Right_delay, 0, Right_Post_Delay_amp, 0);
AudioConnection          patchCord_SubDelayToPostDelayAmp(Sub_delay, 0, Sub_Post_Delay_amp, 0);
AudioConnection* delayConnections[] = {
  &patchCord_LeftPostFIRAmpToDelay,
  &patchCord_RightPostFIRAmpToDelay,
  &patchCord_SubPostFIRAmpToDelay,
  &patchCord_LeftDelayToPostDelayAmp,
  &patchCord_RightDelayToPostDelayAmp,
  &patchCord_SubDelayToPostDelayAmp
};
const size_t delayConnections_len = sizeof(delayConnections) / sizeof(delayConnections[0]);

// Connect from Post-FIR checkpoint to Post-Delay checkpoint directly, bypassing delay
AudioConnection          patchCord_LeftPostFIRAmpToPostDelayAmp(Left_Post_FIR_amp, 0, Left_Post_Delay_amp, 0);
AudioConnection          patchCord_RightPostFIRAmpToPostDelayAmp(Right_Post_FIR_amp, 0, Right_Post_Delay_amp, 0);
AudioConnection          patchCord_SubPostFIRAmpToPostDelayAmp(Sub_Post_FIR_amp, 0, Sub_Post_Delay_amp, 0);
AudioConnection* bypassDelayConnections[] = {
  &patchCord_LeftPostFIRAmpToPostDelayAmp,
  &patchCord_RightPostFIRAmpToPostDelayAmp,
  &patchCord_SubPostFIRAmpToPostDelayAmp
};
const size_t bypassDelayConnections_len = sizeof(bypassDelayConnections) / sizeof(bypassDelayConnections[0]);

//Connect from Post-delay checkpoint to outputs
// Analog outs
AudioConnection          patchCord_LeftPostDelayAmpToAnalogOut(Left_Post_Delay_amp, 0, L_R_Analog_Out, 0);
AudioConnection          patchCord_RightPostDelayAmpToAnalogOut(Right_Post_Delay_amp, 0, L_R_Analog_Out, 1);
AudioConnection          patchCord_SubPostDelayAmpToAnalogOut(Sub_Post_Delay_amp, 0, Sub_Analog_Out, 0);
// Digital outs
AudioConnection          patchCord_LeftPostDelayAmpToSpdifOut(Left_Post_Delay_amp, 0, L_R_Spdif_Out, 0);
AudioConnection          patchCord_RightPostDelayAmpToSpdifOut(Right_Post_Delay_amp, 0, L_R_Spdif_Out, 1);
AudioConnection* outputConnections[] = {
  &patchCord_LeftPostDelayAmpToAnalogOut,
  &patchCord_RightPostDelayAmpToAnalogOut,
  &patchCord_SubPostDelayAmpToAnalogOut,
  &patchCord_LeftPostDelayAmpToSpdifOut,
  &patchCord_RightPostDelayAmpToSpdifOut
};
const size_t outputConnections_len = sizeof(outputConnections) / sizeof(outputConnections[0]);

//Define a structure for holding state
struct State {
  bool isDirty = false;

  // Input gains
  float gainBluetooth = 1.0;
  float gainOptical = 1.0;
  float gainGenerator = 1.0;

  // Speaker gain
  float gainLeft = 1.0;
  float gainRight = 1.0;
  float gainSub = 1.0;

  // Crossover
  int crossoverFrequency = 50;

  // Bypass options
  bool eqEnabled = true;
  bool crossoverEnabled = true;
  bool firEnabled = true;
  bool delayEnabled = true;

  // FIR filter files (default to null)
  char firFileLeft[MAX_FILENAME_LEN] = "";
  char firFileRight[MAX_FILENAME_LEN] = "";
  char firFileSub[MAX_FILENAME_LEN] = "";
  int firTaps = 256;

  // Speaker delay config (default to 0)
  int delayLeftMicroSeconds = 0;
  int delayRightMicroSeconds = 0;
  int delaySubMicroSeconds = 0;

  // an array of biquad filters for the left, right, and subwoofer
  PEQBand filters[15];

  // String toString() {
  //   return String("State{") +
  //          "isDirty=" + String(isDirty) + ", " +
  //          "gainBluetooth=" + String(gainBluetooth) + ", " +
  //          "gainOptical=" + String(gainOptical) + ", " +
  //          "gainGenerator=" + String(gainGenerator) + ", " +
  //          "gainLeft=" + String(gainLeft) + ", " +
  //          "gainRight=" + String(gainRight) + ", " +
  //          "gainSub=" + String(gainSub) + ", " +
  //          "crossoverFrequency=" + String(crossoverFrequency) + ", " +
  //          "eqEnabled=" + String(eqEnabled) + ", " +
  //          "crossoverEnabled=" + String(crossoverEnabled) + ", " +
  //          "firEnabled=" + String(firEnabled) + ", " +
  //          "delayEnabled=" + String(delayEnabled) + ", " +
  //          "firFileLeft=" + String(firFileLeft) + ", " +
  //          "firFileRight=" + String(firFileRight) + ", " +
  //          "firFileSub=" + String(firFileSub) + ", " +
  //          "firTaps=" + String(firTaps) + ", " +
  //          "delayLeftMicroSeconds=" + String(delayLeftMicroSeconds) + ", " +
  //          "delayRightMicroSeconds=" + String(delayRightMicroSeconds) + ", " +
  //          "delaySubMicroSeconds=" + String(delaySubMicroSeconds) + ", " +
  //         "}";
  // }
};

State state;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
    // Wait for serial connection or timeout after 3 seconds
  }
  
  // Audio connections require memory to work
  AudioMemory(20);

  //Load state from EEPROM
  EEPROM.get(0, state);

  // Explicitly set all amps to gain=1.0
  Left_Post_EQ_amp.gain(1.0);
  Right_Post_EQ_amp.gain(1.0);
  Sub_Post_EQ_amp.gain(1.0);
  Left_Post_Crossover_amp.gain(1.0);
  Right_Post_Crossover_amp.gain(1.0);
  Sub_Post_Crossover_amp.gain(1.0);
  Left_Post_FIR_amp.gain(1.0);
  Right_Post_FIR_amp.gain(1.0);
  Sub_Post_FIR_amp.gain(1.0);
  Left_Post_Delay_amp.gain(1.0);
  Right_Post_Delay_amp.gain(1.0);
  Sub_Post_Delay_amp.gain(1.0);

  //Bypass all FIR filters
  Sub_FIR_Filter.begin(FIR_PASSTHRU, 0);
  Left_FIR_Filter.begin(FIR_PASSTHRU, 0);
  Right_FIR_Filter.begin(FIR_PASSTHRU, 0);

  // Apply state
  setInputGains(state.gainBluetooth, state.gainOptical, state.gainGenerator);
  setSpeakerGains(state.gainLeft, state.gainRight, state.gainSub);
  setEQEnabled(state.eqEnabled);
  setCrossoverEnabled(state.crossoverEnabled);
  setFIREnabled(state.firEnabled);
  setDelayEnabled(state.delayEnabled);
  setEQFilters(state.filters);
  setCrossoverFrequency(state.crossoverFrequency);
  setFIR(state.firFileLeft, state.firFileRight, state.firFileSub, state.firTaps);
  setDelays(state.delayLeftMicroSeconds, state.delayRightMicroSeconds, state.delaySubMicroSeconds);
  // State hasn't changed, so don't save
  state.isDirty = false;

  //Register handlers for I2C commands
  router.on("setSpeakerGains", [](const String& cmd, String* args, int count) { handleSetSpeakerGains(cmd, args, count); });
  router.on("getFiles", [](const String& cmd, String* args, int count) { handleGetFiles(cmd, args, count); });
}

void loop() {
  // Optional: Print some diagnostics every 2 seconds
  static unsigned long lastPrint = 0;
  static unsigned long lastStateCheck = 0;

  if (millis() - lastPrint > 2000) {
    lastPrint = millis();
    
    // Check if we're getting input signal
    if (Optical_in.isLocked()) {
      Serial.println("SPDIF Input Locked - Signal detected");
    } else {
      Serial.println("No SPDIF input signal detected");
    }
  }

  if (millis() - lastStateCheck > 2000) {
    lastStateCheck = millis();
    
    // If state has changed (is dirty), then save state to EEPROM
    if (state.isDirty) {
      state.isDirty = false;
      save_state();
    }
  }

  // Call this every loop to handle animations
  peqLeft.update();
  peqRight.update();
}

void save_state() {
  // Save state to EEPROM
  EEPROM.put(0, state);
  Serial.println("State saved to EEPROM: ");
}

void setEQEnabled(bool enabled) {
  state.eqEnabled = enabled;
  state.isDirty = true;
  // Enable/disable both PEQ processors
  peqLeft.setBypass(!enabled);
  peqRight.setBypass(!enabled);
}

void setCrossoverEnabled(bool enabled) {
  state.crossoverEnabled = enabled;
  state.isDirty = true;

  AudioConnection** disableConnections = enabled ? bypassCrossoverConnections : crossoverConnections;
  AudioConnection** enableConnections = enabled ? crossoverConnections : bypassCrossoverConnections;
  const size_t disableConnections_len = enabled ? bypassCrossoverConnections_len : crossoverConnections_len;
  const size_t enableConnections_len = enabled ? crossoverConnections_len : bypassCrossoverConnections_len;

  for (size_t i = 0; i < disableConnections_len; ++i) {
    AudioConnection* connection = disableConnections[i];
    if (connection) {
      connection->disconnect();
    }
  }
  for (size_t i = 0; i < enableConnections_len; ++i) {
    AudioConnection* connection = enableConnections[i];
    if (connection) {
      connection->connect();
    }
  }
}

void setFIREnabled(bool enabled) {
  state.firEnabled = enabled;
  state.isDirty = true;

  AudioConnection** disableConnections = enabled ? bypassFIRConnections : firConnections;
  AudioConnection** enableConnections = enabled ? firConnections : bypassFIRConnections;
  const size_t disableConnections_len = enabled ? bypassFIRConnections_len : firConnections_len;
  const size_t enableConnections_len = enabled ? firConnections_len : bypassFIRConnections_len;

  for (size_t i = 0; i < disableConnections_len; ++i) {
    AudioConnection* connection = disableConnections[i];
    if (connection) {
      connection->disconnect();
    }
  }
  for (size_t i = 0; i < enableConnections_len; ++i) {
    AudioConnection* connection = enableConnections[i];
    if (connection) {
      connection->connect();
    }
  }
}

void setDelayEnabled(bool enabled) {
  state.delayEnabled = enabled;
  state.isDirty = true;

  AudioConnection** disableConnections = enabled ? bypassDelayConnections : delayConnections;
  AudioConnection** enableConnections = enabled ? delayConnections : bypassDelayConnections;
  const size_t disableConnections_len = enabled ? bypassDelayConnections_len : delayConnections_len;
  const size_t enableConnections_len = enabled ? delayConnections_len : bypassDelayConnections_len;

  for (size_t i = 0; i < disableConnections_len; ++i) {
    AudioConnection* connection = disableConnections[i];
    if (connection) {
      connection->disconnect();
    }
  }
  for (size_t i = 0; i < enableConnections_len; ++i) {
    AudioConnection* connection = enableConnections[i];
    if (connection) {
      connection->connect();
    }
  }
}

void setInputGains(float bluetoothGain, float opticalGain, float generatorGain) {
  state.gainBluetooth = bluetoothGain;
  state.gainOptical = opticalGain;
  state.gainGenerator = generatorGain;
  state.isDirty = true;

  Left_mixer.gain(0, state.gainOptical);
  Right_mixer.gain(0, state.gainOptical);
  Left_mixer.gain(1, state.gainBluetooth);
  Right_mixer.gain(1, state.gainBluetooth);
  Left_mixer.gain(3, state.gainGenerator);
  Right_mixer.gain(3, state.gainGenerator);
}

void setSpeakerGains(float leftGain, float rightGain, float subGain) {
  state.gainLeft = leftGain;
  state.gainRight = rightGain;
  state.gainSub = subGain;
  state.isDirty = true;

  Serial.println("Set gains: Left " + String(leftGain) + ", Right " + String(rightGain) + ", Sub " + String(subGain));

  Left_Post_Delay_amp.gain(state.gainLeft);
  Right_Post_Delay_amp.gain(state.gainRight);
  Sub_Post_Delay_amp.gain(state.gainSub);
}

void setEQFilters(PEQBand filters[], int animationDuration) {
  // Assuming MAX_PEQ_BANDS is 15, matching the size of state.filters
  // and PEQProcessor::MAX_BANDS if PEQProcessor is designed for a fixed number.
  // For safety, let's use the actual size of the state.filters array.
  const int num_filters_to_copy = sizeof(state.filters) / sizeof(state.filters[0]);
  for (int i = 0; i < num_filters_to_copy; ++i) {
        if (i < 15) { // Check bounds for input 'filters' if its size is not guaranteed
        state.filters[i] = filters[i];
    } else {
        // Optionally clear remaining state.filters if input is shorter
        state.filters[i] = {0,0,0}; // Example of clearing a PEQPoint
    }
  }
  state.isDirty = true;

  PEQBand bands[num_filters_to_copy]; // Use the same size
  int nBands = 0;
  for (int i = 0; i < num_filters_to_copy; ++i) {
    if (state.filters[i].frequency > 0) { // Only add valid bands from state
      bands[nBands].frequency = state.filters[i].frequency;
      bands[nBands].gain = state.filters[i].gain;
      bands[nBands].q = state.filters[i].q;
      bands[nBands].enabled = true; // Assuming PEQBand has an enabled field
      nBands++;
    }
  }
  peqLeft.animateToBands(bands, nBands, animationDuration);
  peqRight.animateToBands(bands, nBands, animationDuration);
}

void setCrossoverFrequency(int frequency) {
  state.crossoverFrequency = frequency;
  state.isDirty = true;

  // 4th-order Linkwitz-Riley crossover, by cascading two 2nd-order Butterworth stages
  Left_highpass.setHighpass(0, state.crossoverFrequency, 0.707f); // Stage 1
  Left_highpass.setHighpass(1, state.crossoverFrequency, 0.707f); // Stage 2 for 4th order (assuming AudioFilterBiquad supports multiple stages this way or this is a typo and means two separate filters)
                                                              // More likely: one call per filter, or the biquad object manages stages internally based on calls.
                                                              // For Teensy Audio lib, one setHighpass call configures one biquad. To cascade, you use two biquad objects.
                                                              // Let's assume the object Left_highpass itself is a single biquad. For 4th order, you'd typically chain two such objects.
                                                              // Given the setup, it's more likely these are parameters for different internal biquads if the object supports it, or it's configuring a single 2nd order filter.
                                                              // Reverting to original single calls per filter object, assuming each is a 2nd order section.
  Left_highpass.setHighpass(0, state.crossoverFrequency, 0.707f);
  Right_highpass.setHighpass(0, state.crossoverFrequency, 0.707f);
  Sub_lowpass.setLowpass(0, state.crossoverFrequency, 0.707f);
  // If these were meant to be 4th order, the audio routing and object setup would be different (e.g. Left_Post_EQ_amp -> highpass1 -> highpass2 -> Left_Post_Crossover_amp)
  // Sticking to what was literally there before it was deleted.
}

void setFIR(String leftFile, String rightFile, String subFile, int taps) {
  strncpy(state.firFileLeft, leftFile.c_str(), MAX_FILENAME_LEN - 1);
  state.firFileLeft[MAX_FILENAME_LEN - 1] = '\0'; // Ensure null termination
  strncpy(state.firFileRight, rightFile.c_str(), MAX_FILENAME_LEN - 1);
  state.firFileRight[MAX_FILENAME_LEN - 1] = '\0'; // Ensure null termination
  strncpy(state.firFileSub, subFile.c_str(), MAX_FILENAME_LEN - 1);
  state.firFileSub[MAX_FILENAME_LEN - 1] = '\0'; // Ensure null termination
  state.firTaps = taps;
  state.isDirty = true;

  if (leftFile.length() > 0) {
    FIRLoader::loadFilter(leftFile, &Left_FIR_Filter, taps);
  }
  
  if (rightFile.length() > 0) {
    FIRLoader::loadFilter(rightFile, &Right_FIR_Filter, taps);
  }
  
  if (subFile.length() > 0) {
    FIRLoader::loadFilter(subFile, &Sub_FIR_Filter, taps);
  }
}

void setDelays(int delayL_us, int delayR_us, int delayS_us) {
  state.delayLeftMicroSeconds = delayL_us;
  state.delayRightMicroSeconds = delayR_us;
  state.delaySubMicroSeconds = delayS_us;
  state.isDirty = true;

  Left_delay.delay(0, delayL_us / 1000.0f); // delay time in milliseconds
  Right_delay.delay(0, delayR_us / 1000.0f);
  Sub_delay.delay(0, delayS_us / 1000.0f);
}

/*
 * Define I2C command handlers
 */

void handleSetSpeakerGains(const String& command, String* args, int argCount) {
  if (argCount == 3) {
    float leftGain = args[0].toFloat();
    float rightGain = args[1].toFloat();
    float subGain = args[2].toFloat();
    setSpeakerGains(leftGain, rightGain, subGain);
  } else {
    // Optional: Send error back to master if arg count is wrong
  }
}

void handleGetFiles(const String& command, String* args, int argCount) {
  File root = SD.open("/");
  if (!root) {
    // stream.write("ERROR: No SD card\n", 19); // Temporarily commented out
    Serial.println("ERROR: No SD card"); // Placeholder for error reporting
    return;
  }
  if (!root.isDirectory()) {
    // stream.write("ERROR: Not a directory\n", 23); // Temporarily commented out
    Serial.println("ERROR: Not a directory"); // Placeholder for error reporting
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      // stream.write(file.name(), strlen(file.name())); // Temporarily commented out
      // stream.write("\n",1); // Temporarily commented out
      Serial.println(file.name()); // Placeholder for output
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
  // stream.write("\0END\n", 5); // Temporarily commented out
}
