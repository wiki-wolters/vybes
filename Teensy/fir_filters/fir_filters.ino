#include <Audio.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include "FIRLoader.h"
#include "PEQProcessor.h"
#include "I2CCommandRouter.h"
#include "OutputStream.h"
#include "AudioFilterFIRFloat.h"
#include "IntervalTimer.h"

// Create router instance (I2C address 18)
I2CCommandRouter router(0x12);

#define MAX_FILENAME_LEN 64 // Maximum length for FIR filenames

// Audio generators
AudioSynthWaveform       Tone_generator;
AudioSynthNoisePink      pink1;         

//Audio Inputs (Bluetooth, SPDIF, USB)
AudioInputI2S            Bluetooth_in;  
AsyncAudioInputSPDIF3    Optical_in; 
//AudioInputUSB            USB_in;   

// Input mixers
AudioMixer4              Left_mixer;    
AudioMixer4              Right_mixer;   
AudioMixer4              Generator_mixer;   

// Parametric EQ processors (multi-band)
AudioAmplifier           Left_Pre_EQ_amp;
AudioAmplifier           Right_Pre_EQ_amp;
PEQProcessor peqLeft;
PEQProcessor peqRight;

// Post EQ checkpoint, including subwoofer (using amps with gain = 1.0)
AudioAmplifier           Left_Post_EQ_amp;
AudioAmplifier           Right_Post_EQ_amp;
AudioAmplifier           Sub_Post_EQ_amp;
AudioAmplifier           Mono_Post_EQ_amp;

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

// Crossover components (first stage)
AudioFilterStateVariable  Left_highpass;  
AudioFilterStateVariable  Sub_lowpass;    
AudioFilterStateVariable  Right_highpass;

// Crossover components (second stage for 4th order Butterworth)
AudioFilterStateVariable  Left_highpass2;  
AudioFilterStateVariable  Sub_lowpass2;    
AudioFilterStateVariable  Right_highpass2;

// FIR Filters
AudioFilterFIRFloat      Left_FIR_Filter;
AudioFilterFIRFloat      Right_FIR_Filter;
AudioFilterFIRFloat      Sub_FIR_Filter;

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
AudioConnection          patchCord_BluetoothLToLeftMixer(Bluetooth_in, 0, Left_mixer, 1);
AudioConnection          patchCord_BluetoothRToRightMixer(Bluetooth_in, 1, Right_mixer, 1);
//AudioConnection          patchCord_USBLToLeftMixer(USB_in, 0, Left_mixer, 2);
//AudioConnection          patchCord_USBRToRightMixer(USB_in, 0, Right_mixer, 2);
AudioConnection* externalInputConnections[] = {
  &patchCord_OpticalLToLeftMixer,
  &patchCord_OpticalRToRightMixer,
  &patchCord_BluetoothLToLeftMixer,
  &patchCord_BluetoothRToRightMixer,
  //&patchCord_USBLToLeftMixer,
  //&patchCord_USBRToRightMixer
};
const size_t externalInputConnections_len = sizeof(externalInputConnections) / sizeof(externalInputConnections[0]);

// Patchcords for PEQ processors
AudioConnection patchCord_LeftMixerToPreEQ(Left_mixer, 0, Left_Pre_EQ_amp, 0);
AudioConnection patchCord_RightMixerToPreEQ(Right_mixer, 0, Right_Pre_EQ_amp, 0);
AudioConnection patchCord_LeftPreEQToPEQ(Left_Pre_EQ_amp, 0, peqLeft, 0);
AudioConnection patchCord_RightPreEQToPEQ(Right_Pre_EQ_amp, 0, peqRight, 0);
AudioConnection patchCord_PEQLeftToPostEQ(peqLeft, 0, Left_Post_EQ_amp, 0);
AudioConnection patchCord_PEQRightToPostEQ(peqRight, 0, Right_Post_EQ_amp, 0);
AudioConnection patchCord_PEQLeftToMonoMixer(Left_Post_EQ_amp, 0, Mono_mixer, 0);
AudioConnection patchCord_PEQRightToMonoMixer(peqRight, 0, Mono_mixer, 1);
AudioConnection patchCord_PEQMonoToMonoPostEQ(Mono_mixer, 0, Mono_Post_EQ_amp, 0); // Added source port 0 for Mono_mixer
AudioConnection* peqConnections[] = {
  &patchCord_LeftMixerToPreEQ,
  &patchCord_RightMixerToPreEQ,
  &patchCord_LeftPreEQToPEQ,
  &patchCord_RightPreEQToPEQ,
  &patchCord_PEQLeftToPostEQ,
  &patchCord_PEQRightToPostEQ,
  &patchCord_PEQLeftToMonoMixer,
  &patchCord_PEQRightToMonoMixer,
  &patchCord_PEQMonoToMonoPostEQ
};
const size_t peqConnections_len = sizeof(peqConnections) / sizeof(peqConnections[0]);

// Connect from Post-EQ checkpoint to Post-Crossover checkpoint via crossover
AudioConnection          patchCord_LeftPostEQAmpToHighpass1(Left_Post_EQ_amp, 0, Left_highpass, 0);
AudioConnection          patchCord_LeftHighpassToHighpass2(Left_highpass, 2, Left_highpass2, 0); // Output 2 = highpass
AudioConnection          patchCord_LeftHighpass2ToPostCrossoverAmp(Left_highpass2, 2, Left_Post_Crossover_amp, 0);  // Output 2 = highpass

AudioConnection          patchCord_RightPostEQAmpToHighpass1(Right_Post_EQ_amp, 0, Right_highpass, 0);
AudioConnection          patchCord_RightHighpassToHighpass2(Right_highpass, 2, Right_highpass2, 0); // Output 2 = highpass
AudioConnection          patchCord_RightHighpass2ToPostCrossoverAmp(Right_highpass2, 2, Right_Post_Crossover_amp, 0);  // Output 2 = highpass

AudioConnection          patchCord_MonoPostEQAmpToSubLowpass1(Mono_Post_EQ_amp, 0, Sub_lowpass, 0);
AudioConnection          patchCord_SubLowpassToSubLowpass2(Sub_lowpass, 0, Sub_lowpass2, 0); // Output 0 = lowpass
AudioConnection          patchCord_SubLowpass2ToPostCrossoverAmp(Sub_lowpass2, 0, Sub_Post_Crossover_amp, 0);  // Output 0 = lowpass

AudioConnection* crossoverConnections[] = {
  &patchCord_LeftPostEQAmpToHighpass1,
  &patchCord_LeftHighpassToHighpass2,
  &patchCord_LeftHighpass2ToPostCrossoverAmp,
  &patchCord_RightPostEQAmpToHighpass1,
  &patchCord_RightHighpassToHighpass2,
  &patchCord_RightHighpass2ToPostCrossoverAmp,
  &patchCord_MonoPostEQAmpToSubLowpass1,
  &patchCord_SubLowpassToSubLowpass2,
  &patchCord_SubLowpass2ToPostCrossoverAmp
};
const size_t crossoverConnections_len = sizeof(crossoverConnections) / sizeof(crossoverConnections[0]);

// Connect from Post-EQ checkpoint to Post-Crossover checkpoint directly, bypassing crossover
AudioConnection          patchCord_LeftPostEQAmpToPostCrossoverAmp(Left_Post_EQ_amp, 0, Left_Post_Crossover_amp, 0);
AudioConnection          patchCord_RightPostEQAmpToPostCrossoverAmp(Right_Post_EQ_amp, 0, Right_Post_Crossover_amp, 0);
AudioConnection          patchCord_MonoPostEQAmpToSubPostCrossoverAmp(Mono_Post_EQ_amp, 0, Sub_Post_Crossover_amp, 0);
AudioConnection* bypassCrossoverConnections[] = {
  &patchCord_LeftPostEQAmpToPostCrossoverAmp,
  &patchCord_RightPostEQAmpToPostCrossoverAmp,
  &patchCord_MonoPostEQAmpToSubPostCrossoverAmp
};
const size_t bypassCrossoverConnections_len = sizeof(bypassCrossoverConnections) / sizeof(bypassCrossoverConnections[0]);

// Connect from Post-Crossover checkpoint to Post-FIR checkpoint via FIR
AudioConnection          patchCord_LeftPostCrossoverAmpToFIR(Left_Post_Crossover_amp, 0, Left_FIR_Filter, 0);
AudioConnection          patchCord_LeftFIRToPostFIRAmp(Left_FIR_Filter, 0, Left_Post_FIR_amp, 0);
AudioConnection          patchCord_RightPostCrossoverAmpToFIR(Right_Post_Crossover_amp, 0, Right_FIR_Filter, 0);
AudioConnection          patchCord_RightFIRToPostFIRAmp(Right_FIR_Filter, 0, Right_Post_FIR_amp, 0);
AudioConnection          patchCord_SubPostCrossoverAmpToFIR(Sub_Post_Crossover_amp, 0, Sub_FIR_Filter, 0);
AudioConnection          patchCord_SubFIRToPostFIRAmp(Sub_FIR_Filter, 0, Sub_Post_FIR_amp, 0);
AudioConnection* firConnections[] = {
  &patchCord_LeftPostCrossoverAmpToFIR,
  &patchCord_LeftFIRToPostFIRAmp,
  &patchCord_RightPostCrossoverAmpToFIR,
  &patchCord_RightFIRToPostFIRAmp,
  &patchCord_SubPostCrossoverAmpToFIR,
  &patchCord_SubFIRToPostFIRAmp
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
//Analog outs
AudioConnection          patchCord_LeftPostDelayAmpToAnalogOut(Left_Post_Delay_amp, 0, L_R_Analog_Out, 0);
AudioConnection          patchCord_RightPostDelayAmpToAnalogOut(Right_Post_Delay_amp, 0, L_R_Analog_Out, 1);
AudioConnection          patchCord_SubPostDelayAmpToAnalogOutL(Sub_Post_Delay_amp, 0, Sub_Analog_Out, 0);
AudioConnection          patchCord_SubPostDelayAmpToAnalogOutR(Sub_Post_Delay_amp, 0, Sub_Analog_Out, 1);
// Digital outs
AudioConnection          patchCord_LeftPostDelayAmpToSpdifOut(Left_Post_Delay_amp, 0, L_R_Spdif_Out, 0);
AudioConnection          patchCord_RightPostDelayAmpToSpdifOut(Right_Post_Delay_amp, 0, L_R_Spdif_Out, 1);
AudioConnection* outputConnections[] = {
  &patchCord_LeftPostDelayAmpToAnalogOut,
  &patchCord_RightPostDelayAmpToAnalogOut,
  &patchCord_SubPostDelayAmpToAnalogOutL,
  &patchCord_SubPostDelayAmpToAnalogOutR,
  &patchCord_LeftPostDelayAmpToSpdifOut,
  &patchCord_RightPostDelayAmpToSpdifOut
};
const size_t outputConnections_len = sizeof(outputConnections) / sizeof(outputConnections[0]);

const int CURRENT_VERSION = 3;
bool firFilesPending = false;

//Define a structure for holding state
struct State {
  int version = CURRENT_VERSION;
  bool isDirty = false;

  // Input gains
  float gainBluetooth = 1.0;
  float gainOptical = 1.0;
  float gainUSB = 1.0;
  float gainGenerator = 1.0;

  // Speaker gain
  float gainLeft = 1.0;
  float gainRight = 1.0;
  float gainSub = 1.0;

  // Crossover
  uint16_t crossoverFrequency = 50;

  // Bypass options
  bool eqEnabled = true;
  bool crossoverEnabled = true;
  bool firEnabled = true;
  bool delayEnabled = true;

  // FIR filter files (default to null)
  char firFileLeft[MAX_FILENAME_LEN] = "";
  char firFileRight[MAX_FILENAME_LEN] = "";
  char firFileSub[MAX_FILENAME_LEN] = "";

  // Speaker delay config (default to 0)
  int delayLeftMicroSeconds = 0;
  int delayRightMicroSeconds = 0;
  int delaySubMicroSeconds = 0;

  // an array of biquad filters for the left, right, and subwoofer
  PEQBand filters[MAX_PEQ_BANDS];
};

State state;

size_t getConsecutiveActiveFilterCount() {
  size_t count = 0;
  for (int i = 0; i < MAX_PEQ_BANDS; i++) {
    if (state.filters[i].enabled) {
      count++;
    } else {
      break; // Stop at first disabled filter
    }
  }
  return count;
}

void setup() {
  Serial.begin(9600);
  Serial.println();
  Serial.println("=== SPDIF to I2S Debug Test ===");
  Serial.println("Setup starting...");

  if (CrashReport) {
    Serial.print(CrashReport);
    CrashReport.clear();
  }

  pinMode(LED_BUILTIN, OUTPUT);
  
  // Audio connections require memory to work
  Serial.println("Allocating audio memory");
  AudioMemory(60);
  Serial.println("=== Audio Memory Debug ===");
  Serial.print("AudioMemoryUsage(): ");
  Serial.println(AudioMemoryUsage());
  Serial.print("AudioMemoryUsageMax(): ");
  Serial.println(AudioMemoryUsageMax());
  Serial.println("========================");

  // Initialize PEQ processors
  peqLeft.begin(AUDIO_SAMPLE_RATE);
  peqRight.begin(AUDIO_SAMPLE_RATE);

  // Set pre-EQ gain to provide headroom for EQ boosts
  Left_Pre_EQ_amp.gain(0.5);
  Right_Pre_EQ_amp.gain(0.5);

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

  // Set mono mixer amp gain to 0.5
  Mono_mixer.gain(0, 0.5);
  Mono_mixer.gain(1, 0.5);

  // Apply state
  Serial.println("Applying state");
  setInputGains(state.gainBluetooth, state.gainOptical, state.gainGenerator);
  setSpeakerGains(state.gainLeft, state.gainRight, state.gainSub);
  setEQEnabled(state.eqEnabled);
  setFIREnabled(state.firEnabled);
  setDelayEnabled(state.delayEnabled);
  setEQFilters(state.filters, getConsecutiveActiveFilterCount());
  setCrossoverFrequency(state.crossoverFrequency);
  setCrossoverEnabled(state.crossoverEnabled);
  setFIR(state.firFileLeft, state.firFileRight, state.firFileSub);
  setDelays(state.delayLeftMicroSeconds, state.delayRightMicroSeconds, state.delaySubMicroSeconds);
  // State hasn't changed, so don't save
  state.isDirty = false;

  // Add this at the very end of setup(), after state.isDirty = false;
  Serial.println("=== End of Setup Memory Usage ===");
  Serial.print("AudioMemoryUsage(): ");
  Serial.println(AudioMemoryUsage());
  Serial.print("AudioMemoryUsageMax(): ");
  Serial.println(AudioMemoryUsageMax());
  Serial.println("================================");

  //Register handlers for I2C commands
  Serial.println("Registering I2C handlers");
  router.on("setSpeakerGains", handleSetSpeakerGains);
  router.on("getFiles", handleGetFiles);
  router.on("setInputGains", handleSetInputGains);
  router.on("setCrossoverFrequency", handleSetCrossoverFrequency);
  router.on("setEqEnabled", handleSetEQEnabled);
  router.on("setCrossoverEnabled", handleSetCrossoverEnabled);
  router.on("setFirEnabled", handleSetFIREnabled);
  router.on("setDelayEnabled", handleSetDelayEnabled);
  router.on("setFir", handleSetFIR);
  router.on("setDelays", handleSetDelays);
  router.on("setEq", handleSetEQFilter);
  router.on("resetEqFilters", handleResetEQFilters);
  router.begin();
}

void loop() {
  // Optional: Print some diagnostics every 2 seconds
  static unsigned long lastPrint = 0;

  if (millis() - lastPrint > 5000) {
    lastPrint = millis();
    
    // Check if we're getting input signal
    // if (Optical_in.isLocked()) {
    //   Serial.println("SPDIF Input Locked - Signal detected");
    //   Serial.print("Audio memory usage max: ");
    //   Serial.println(AudioMemoryUsageMax());
    // } else {
    //   Serial.println("No SPDIF input signal detected");
    // }
  }

  // if (firFilesPending) {
  //   loadFirFiles();
  //   firFilesPending = false;
  // }

  // Add this as the very first thing in loop()
  static bool firstLoop = true;
  if (firstLoop) {
    Serial.println("=== First Loop Iteration ===");
    Serial.print("Memory at start of first loop: ");
    Serial.println(AudioMemoryUsage());
    firstLoop = false;
  }

  static unsigned long lastMemoryCheck = 0;
  if (millis() - lastMemoryCheck > 5000) {
    lastMemoryCheck = millis();
    
    Serial.print("Audio Memory Usage: ");
    Serial.println(AudioMemoryUsage());
    // Don't call AudioMemoryUsageMaxReset() yet
  }
  router.loop();
}

void setEQEnabled(bool enabled) {
  Serial.println(String("Set EQ enabled: ") + (enabled ? "yes" : "no"));
  state.eqEnabled = enabled;
  state.isDirty = true;
  peqLeft.setBypass(!enabled);
  peqRight.setBypass(!enabled);
}

void setCrossoverEnabled(bool enabled) {
  Serial.println(String("Set crossover enabled: ") + (enabled ? "yes" : "no"));
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
  Serial.println(String("Set fir enabled: ") + (enabled ? "yes" : "no"));
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
  Serial.println(String("Set delay enabled: ") + (enabled ? "yes" : "no"));
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
  Serial.println("Set input gains: bluetooth " + String(bluetoothGain) + ", optical " + String(opticalGain));
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
  Serial.println("Set gains: Left " + String(leftGain) + ", Right " + String(rightGain) + ", Sub " + String(subGain));
  state.gainLeft = leftGain;
  state.gainRight = rightGain;
  state.gainSub = subGain;
  state.isDirty = true;
  Left_Post_Delay_amp.gain(state.gainLeft);
  Right_Post_Delay_amp.gain(state.gainRight);
  Sub_Post_Delay_amp.gain(state.gainSub);
}

void setEQFilters(PEQBand filters[], int animationDuration) {
  int8_t nBands = getConsecutiveActiveFilterCount();
  peqLeft.animateToBands(filters, nBands, animationDuration);
  peqRight.animateToBands(filters, nBands, animationDuration);
}

void setCrossoverFrequency(uint16_t frequency) {
  Serial.println("Set crossover freq: " + String(frequency));
  state.crossoverFrequency = frequency;
  state.isDirty = true;

  // Resonance parameter: 0.707 for Butterworth response
  float resonance = 0.707f;
  
  // High-pass for left and right channels (use output port 2)
  Left_highpass.frequency(state.crossoverFrequency);
  Left_highpass.resonance(resonance);
  
  Right_highpass.frequency(state.crossoverFrequency);
  Right_highpass.resonance(resonance);

  // Low-pass for the subwoofer (use output port 0)
  Sub_lowpass.frequency(state.crossoverFrequency);
  Sub_lowpass.resonance(resonance);

  // Configure second stage of high-pass filters
  Left_highpass2.frequency(state.crossoverFrequency);
  Left_highpass2.resonance(resonance);
  
  Right_highpass2.frequency(state.crossoverFrequency);
  Right_highpass2.resonance(resonance);

  // Configure second stage of low-pass filter
  Sub_lowpass2.frequency(state.crossoverFrequency);
  Sub_lowpass2.resonance(resonance);
}

void setFIR(String leftFile, String rightFile, String subFile) {
  Serial.println("Set fir filters: left " + leftFile + ", right " + rightFile + ", sub " + subFile);
  strncpy(state.firFileLeft, leftFile.c_str(), MAX_FILENAME_LEN - 1);
  state.firFileLeft[MAX_FILENAME_LEN - 1] = '\0';
  strncpy(state.firFileRight, rightFile.c_str(), MAX_FILENAME_LEN - 1);
  state.firFileRight[MAX_FILENAME_LEN - 1] = '\0';
  strncpy(state.firFileSub, subFile.c_str(), MAX_FILENAME_LEN - 1);
  state.firFileSub[MAX_FILENAME_LEN - 1] = '\0';
  state.isDirty = true;

  firFilesPending = true;
}

void loadFirFiles() {
  if (state.firEnabled) {
    for (size_t i = 0; i < firConnections_len; ++i) {
      firConnections[i]->disconnect();
    }
  }
  
  uint16_t actualTaps = 0;
  
  // Load left channel FIR
  if (strlen(state.firFileLeft) > 0) {
    actualTaps = 0;
    float* coeffs = FIRLoader::loadCoefficients(state.firFileLeft, actualTaps);
    if (coeffs) {
      Left_FIR_Filter.loadCoefficients(coeffs, actualTaps);
      delete[] coeffs;
    } else {
      Left_FIR_Filter.loadCoefficients(nullptr, 0);
    }
  } else {
    Left_FIR_Filter.loadCoefficients(nullptr, 0);
  }
  
  // Load right channel FIR
  if (strlen(state.firFileRight) > 0) {
    actualTaps = 0;
    float* coeffs = FIRLoader::loadCoefficients(state.firFileRight, actualTaps);
    if (coeffs) {
      Right_FIR_Filter.loadCoefficients(coeffs, actualTaps);
      delete[] coeffs;
    } else {
      Right_FIR_Filter.loadCoefficients(nullptr, 0);
    }
  } else {
    Right_FIR_Filter.loadCoefficients(nullptr, 0);
  }
  
  // Load subwoofer FIR
  if (strlen(state.firFileSub) > 0) {
    actualTaps = 0;
    float* coeffs = FIRLoader::loadCoefficients(state.firFileSub, actualTaps);
    if (coeffs) {
      Sub_FIR_Filter.loadCoefficients(coeffs, actualTaps);
      delete[] coeffs;
    } else {
      Sub_FIR_Filter.loadCoefficients(nullptr, 0);
    }
  } else {
    Sub_FIR_Filter.loadCoefficients(nullptr, 0);
  }
  
  if (state.firEnabled) {
    for (size_t i = 0; i < firConnections_len; ++i) {
      firConnections[i]->connect();
    }
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

void resetEQFilters(int fromIndex) {
  for (int i = fromIndex; i < MAX_PEQ_BANDS; i++) {
    state.filters[i].enabled = false;
    state.filters[i].frequency = 1000.0f;
    state.filters[i].gain = 0.0f;
    state.filters[i].q = 1.0f;
  }
  state.isDirty = true;
  setEQFilters(state.filters, getConsecutiveActiveFilterCount());
}

/*
 * Define I2C command handlers
 */

void handleSetSpeakerGains(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 3) {
    float leftGain = args[0].toFloat();
    float rightGain = args[1].toFloat();
    float subGain = args[2].toFloat();
    setSpeakerGains(leftGain, rightGain, subGain);
  } else {
    // Optional: Send error back to master if arg count is wrong
  }
}

void handleGetFiles(const String& command, String* args, int argCount, OutputStream& stream) {
  File root = SD.open("/");
  if (!root) {
    stream.write("ERROR: No SD card\n", 19);
    return;
  }
  if (!root.isDirectory()) {
    stream.write("ERROR: Not a directory\n", 24);
    root.close();
    return;
  }

  bool firstFile = true;
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      if (!firstFile) {
        stream.write("\n", 1);
      }
      stream.write(file.name(), strlen(file.name()));
      firstFile = false;
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
  
  // Add a final newline to mark the end of the file list
  stream.write("\n", 1);
}

void handleSetInputGains(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 3) {
    setInputGains(args[0].toFloat(), args[1].toFloat(), args[2].toFloat());
  }
}

void handleSetCrossoverFrequency(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    setCrossoverFrequency(strtoul(args[0].c_str(), NULL, 10));
  }
}

void handleSetEQEnabled(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    setEQEnabled(args[0].toInt() == 1);
  }
}

void handleSetCrossoverEnabled(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    setCrossoverEnabled(args[0].toInt() == 1);
  }
}

void handleSetFIREnabled(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    setFIREnabled(args[0].toInt() == 1);
  }
}

void handleSetDelayEnabled(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    setDelayEnabled(args[0].toInt() == 1);
  }
}

void handleSetFIR(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 3) {
    setFIR(args[0], args[1], args[2]);
  }
}

void handleSetDelays(const String& command, String* args, int argCount, OutputStream& stream) {
  Serial.print("Received setDelays command with ");
  Serial.print(argCount);
  Serial.println(" arguments");
  
  if (argCount == 3) {
    Serial.print("Args: ");
    Serial.print(args[0]); Serial.print(", ");
    Serial.print(args[1]); Serial.print(", ");
    Serial.println(args[2]);
    
    setDelays(args[0].toInt(), args[1].toInt(), args[2].toInt());
    stream.write("OK", 2);  // Send acknowledgment back
  } else {
    Serial.println("Wrong number of arguments for setDelays");
    stream.write("ERROR", 5);
  }
}

void handleResetEQFilters(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    resetEQFilters(args[0].toInt());
  }
}

void handleSetEQFilter(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 4) {
    int index = args[0].toInt();
    float frequency = args[1].toFloat();
    float q = args[2].toFloat();
    float gain = args[3].toFloat();

    if (index >= 0 && index < MAX_PEQ_BANDS) {
      // Update the central state
      state.filters[index].enabled = true;
      state.filters[index].frequency = frequency;
      state.filters[index].q = q;
      state.filters[index].gain = gain;
      state.isDirty = true;

      // Directly apply the change to the PEQ processors
      peqLeft.setBand(index, frequency, gain, q, true);
      peqRight.setBand(index, frequency, gain, q, true);
    }
  }
}
