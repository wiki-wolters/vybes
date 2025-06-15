#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <SD_FAT.h>
#include "FIRLoader.h"
#include "PEQProcessor.h"

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
AudioConnection* generatorConnections[] = {
  AudioConnection          patchCord_GenToneToMixer(Tone_generator, 0, Generator_mixer, 0),
  AudioConnection          patchCord_PinkToMixer(pink1, 0, Generator_mixer, 1),
  //Connect generator mixer to left and right mixers
  AudioConnection          patchCord_GenMixerToLeftMixer(Generator_mixer, Left_mixer, 3),
  AudioConnection          patchCord_GenMixerToRightMixer(Generator_mixer, Right_mixer, 3)
};

// External input connections
AudioConnection* externalInputConnections[] = {
  AudioConnection          patchCord_OpticalLToLeftMixer(Optical_in, 0, Left_mixer, 0),
  AudioConnection          patchCord_OpticalRToRightMixer(Optical_in, 1, Right_mixer, 0),
  // AudioConnection          patchCord_BluetoothLToLeftMixer(Bluetooth_in, 0, Left_mixer, 1),
  // AudioConnection          patchCord_BluetoothRToRightMixer(Bluetooth_in, 1, Right_mixer, 1)
};

// Patchcords for PEQ processors
AudioConnection* peqConnections[] = {
  AudioConnection patchCord_LeftMixerToPEQ(Left_mixer, 0, peqLeft, 0),
  AudioConnection patchCord_RightMixerToPEQ(Right_mixer, 0, peqRight, 0),
  AudioConnection patchCord_PEQLeftToPostEQ(peqLeft, 0, Left_Post_EQ_amp, 0),
  AudioConnection patchCord_PEQRightToPostEQ(peqRight, 0, Right_Post_EQ_amp, 0),
  AudioConnection patchCord_PEQLeftToMonoMixer(peqLeft, 0, Mono_mixer, 0),
  AudioConnection patchCord_PEQRightToMonoMixer(peqRight, 0, Mono_mixer, 1),
  AudioConnection patchCord_PEQMonoToMonoPostEQ(Mono_mixer, Mono_Post_EQ_amp, 0)
};

// Connect from Post-EQ checkpoint to Post-Crossover checkpoint via crossover
AudioConnection* crossoverConnections[] = {
  // Left
  AudioConnection          patchCord_LeftPostEQAmpToHighpass1(Left_Post_EQ_amp, Left_highpass),
  AudioConnection          patchCord_LeftHighpassToPostCrossoverAmp(Left_highpass, Left_Post_Crossover_amp),
  // Right
  AudioConnection          patchCord_RightPostEQAmpToHighpass1(Right_Post_EQ_amp, Right_highpass),
  AudioConnection          patchCord_RightHighpassToPostCrossoverAmp(Right_highpass, Right_Post_Crossover_amp),
  // Sub
  AudioConnection          patchCord_MonoPostEQAmpToSubLowpass1(Mono_Post_EQ_amp, Sub_lowpass),
  AudioConnection          patchCord_SubLowpassToPostCrossoverAmp(Sub_lowpass, Sub_Post_Crossover_amp),
};

// Connect from Post-EQ checkpoint to Post-Crossover checkpoint directly, bypassing crossover
AudioConnection* bypassCrossoverConnections[] = {
  AudioConnection          patchCord_LeftPostEQAmpToPostCrossoverAmp(Left_Post_EQ_amp, Left_Post_Crossover_amp),
  AudioConnection          patchCord_RightPostEQAmpToPostCrossoverAmp(Right_Post_EQ_amp, Right_Post_Crossover_amp),
  AudioConnection          patchCord_SubPostEQAmpToPostCrossoverAmp(Sub_Post_EQ_amp, Sub_Post_Crossover_amp),
};

// Connect from Post-Crossover checkpoint to Post-FIR checkpoint via FIR
AudioConnection* firConnections[] = {
  AudioConnection          patchCord_LeftPostCrossoverAmpToFIR(Left_Post_Crossover_amp, Left_FIR_Filter),
  AudioConnection          patchCord_RightPostCrossoverAmpToFIR(Right_Post_Crossover_amp, Right_FIR_Filter),
  AudioConnection          patchCord_SubPostCrossoverAmpToFIR(Sub_Post_Crossover_amp, Sub_FIR_Filter),
};

// Connect from Post-Crossover checkpoint to Post-FIR checkpoint directly, bypassing FIR
AudioConnection* bypassFIRConnections[] = {
  AudioConnection          patchCord_LeftPostCrossoverAmpToPostFIRAmp(Left_Post_Crossover_amp, Left_Post_FIR_amp),
  AudioConnection          patchCord_RightPostCrossoverAmpToPostFIRAmp(Right_Post_Crossover_amp, Right_Post_FIR_amp),
  AudioConnection          patchCord_SubPostCrossoverAmpToPostFIRAmp(Sub_Post_Crossover_amp, Sub_Post_FIR_amp),
};

// Connect from Post-FIR checkpoint to Post-Delay checkpoint via delay
AudioConnection* delayConnections[] = {
  AudioConnection          patchCord_LeftPostFIRAmpToDelay(Left_Post_FIR_amp, Left_delay),
  AudioConnection          patchCord_RightPostFIRAmpToDelay(Right_Post_FIR_amp, Right_delay),
  AudioConnection          patchCord_SubPostFIRAmpToDelay(Sub_Post_FIR_amp, Sub_delay),
  AudioConnection          patchCord_LeftDelayToPostDelayAmp(Left_delay, Left_Post_Delay_amp, 0),
  AudioConnection          patchCord_RightDelayToPostDelayAmp(Right_delay, Right_Post_Delay_amp, 0),
  AudioConnection          patchCord_SubDelayToPostDelayAmp(Sub_delay, Sub_Post_Delay_amp, 0),
};

// Connect from Post-FIR checkpoint to Post-Delay checkpoint directly, bypassing delay
AudioConnection* bypassDelayConnections[] = {
  AudioConnection          patchCord_LeftPostFIRAmpToPostDelayAmp(Left_Post_FIR_amp, Left_Post_Delay_amp),
  AudioConnection          patchCord_RightPostFIRAmpToPostDelayAmp(Right_Post_FIR_amp, Right_Post_Delay_amp),
  AudioConnection          patchCord_SubPostFIRAmpToPostDelayAmp(Sub_Post_FIR_amp, Sub_Post_Delay_amp),
};

//Connect from Post-delay checkpoint to outputs
AudioConnection* outputConnections[] = {
  // Analog outs
  AudioConnection          patchCord_LeftPostDelayAmpToAnalogOut(Left_Post_Delay_amp, L_R_Analog_Out, 0),
  AudioConnection          patchCord_RightPostDelayAmpToAnalogOut(Right_Post_Delay_amp, L_R_Analog_Out, 1),
  AudioConnection          patchCord_SubPostDelayAmpToAnalogOut(Sub_Post_Delay_amp, Sub_Analog_Out, 0),
  // Digital outs
  AudioConnection          patchCord_LeftPostDelayAmpToSpdifOut(Left_Post_Delay_amp, L_R_Spdif_Out, 0),
  AudioConnection          patchCord_RightPostDelayAmpToSpdifOut(Right_Post_Delay_amp, L_R_Spdif_Out, 1),
};

struct PEQPoint {
  float frequency;
  float gain;
  float q;

  String toString() {
    return "PEQ{" +
           "frequency=" + String(frequency) + ", " +
           "gain=" + String(gain) + ", " +
           "q=" + String(q) + ", " +
           "}";
  }
};;

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
  String firFileLeft = "";
  String firFileRight = "";
  String firFileSub = "";
  int firTaps = 256;

  // Speaker delay config (default to 0)
  int delayLeftMicroSeconds = 0;
  int delayRightMicroSeconds = 0;
  int delaySubMicroSeconds = 0;

  // an array of biquad filters for the left, right, and subwoofer
  PEQPoint filters[15];

  String toString() {
    return "State{" +
           "isDirty=" + String(isDirty) + ", " +
           "gainBluetooth=" + String(gainBluetooth) + ", " +
           "gainOptical=" + String(gainOptical) + ", " +
           "gainGenerator=" + String(gainGenerator) + ", " +
           "gainLeft=" + String(gainLeft) + ", " +
           "gainRight=" + String(gainRight) + ", " +
           "gainSub=" + String(gainSub) + ", " +
           "crossoverFrequency=" + String(crossoverFrequency) + ", " +
           "eqEnabled=" + String(eqEnabled) + ", " +
           "crossoverEnabled=" + String(crossoverEnabled) + ", " +
           "firEnabled=" + String(firEnabled) + ", " +
           "delayEnabled=" + String(delayEnabled) + ", " +
           "firFileLeft=" + firFileLeft + ", " +
           "firFileRight=" + firFileRight + ", " +
           "firFileSub=" + firFileSub + ", " +
           "firTaps=" + String(firTaps) + ", " +
           "delayLeftMicroSeconds=" + String(delayLeftMicroSeconds) + ", " +
           "delayRightMicroSeconds=" + String(delayRightMicroSeconds) + ", " +
           "delaySubMicroSeconds=" + String(delaySubMicroSeconds) + ", " +
          "}";
  }
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
  peq.update();
}

void save_state() {
  // Save state to EEPROM
  EEPROM.put(0, state);
  Serial.println("State saved to EEPROM: " + state.toString());
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

  const disableConnections = enabled ? bypassCrossoverConnections : crossoverConnections;
  const enableConnections = enabled ? crossoverConnections : bypassCrossoverConnections;

  //Disable connections that bypass crossover
  foreach (AudioConnection* connection, disableConnections) {
    connection->disconnect();
  }

  //Enable connections that use crossover
  foreach (AudioConnection* connection, enableConnections) {
    connection->connect();
  }
}

void setFIREnabled(bool enabled) {
  state.firEnabled = enabled;
  state.isDirty = true;

  const disableConnections = enabled ? bypassFIRConnections : firConnections;
  const enableConnections = enabled ? firConnections : bypassFIRConnections;

  //Disable connections that bypass FIR
  foreach (AudioConnection* connection, disableConnections) {
    connection->disconnect();
  }

  //Enable connections that use FIR
  foreach (AudioConnection* connection, enableConnections) {
    connection->connect();
  }
}

void setDelayEnabled(bool enabled) {
  state.delayEnabled = enabled;
  state.isDirty = true;

  const disableConnections = enabled ? bypassDelayConnections : delayConnections;
  const enableConnections = enabled ? delayConnections : bypassDelayConnections;

  //Disable connections that bypass delay
  foreach (AudioConnection* connection, disableConnections) {
    connection->disconnect();
  }

  //Enable connections that use delay
  foreach (AudioConnection* connection, enableConnections) {
    connection->connect();
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

  Left_Post_Delay_amp.gain(state.gainLeft);
  Right_Post_Delay_amp.gain(state.gainRight);
  Sub_Post_Delay_amp.gain(state.gainSub);
}

void setEQFilters(PEQPoint filters[], int animationDuration = 20) {
  state.filters = filters;
  state.isDirty = true;
  // Convert PEQPoint to PEQBand and update both channels
  PEQBand bands[MAX_PEQ_BANDS];
  int nBands = 0;
  for (int i = 0; i < MAX_PEQ_BANDS; ++i) {
    if (filters[i].frequency > 0) { // Only add valid bands
      bands[nBands].frequency = filters[i].frequency;
      bands[nBands].gain = filters[i].gain;
      bands[nBands].q = filters[i].q;
      bands[nBands].enabled = true;
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
  Left_highpass.setFrequency(0, state.crossoverFrequency, 0.707);
  Left_highpass.setFrequency(1, state.crossoverFrequency, 0.707);
  Right_highpass.setFrequency(0, state.crossoverFrequency, 0.707);
  Right_highpass.setFrequency(1, state.crossoverFrequency, 0.707);
  Sub_lowpass.setFrequency(0, state.crossoverFrequency, 0.707);
  Sub_lowpass.setFrequency(1, state.crossoverFrequency, 0.707);
}

void setFIR(String leftFile, String rightFile, String subFile, int taps) {
  state.firFileLeft = leftFile;
  state.firFileRight = rightFile;
  state.firFileSub = subFile;
  state.firTaps = taps;
  state.isDirty = true;

  // Load FIR filters - now just simple one-liners!
  if (leftFile != "") {
    FIRLoader::loadFilter(leftFile, &Left_FIR_Filter, taps);
  }
  
  if (rightFile != "") {
    FIRLoader::loadFilter(rightFile, &Right_FIR_Filter, taps);
  }
  
  if (subFile != "") {
    FIRLoader::loadFilter(subFile, &Sub_FIR_Filter, taps);
  }
}
