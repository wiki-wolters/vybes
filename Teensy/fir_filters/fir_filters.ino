#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <SD_FAT.h>
#include "FIRLoader.h"

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

// Parametric EQs
AudioFilterBiquad Left_EQ[15];
AudioFilterBiquad Right_EQ[15];
// Patchcord arrays for EQ chains
AudioConnection* patchCord_LeftEQ[16]; // 15 between biquads, 1 from mixer
AudioConnection* patchCord_RightEQ[16]; // 15 between biquads, 1 from mixer


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

// Connect from input mixers to Post-EQ checkpoint via EQ
// EQ chain connections will be dynamically (re)built as needed in setEQFilters
AudioConnection* eqConnections[16*2+3]; // Enough for both channels and mono

// Connect from input mixers to Post-EQ checkpoint directly, bypassing EQ
AudioConnection* bypassEQConnections[] = {
  AudioConnection          patchCord_LeftMixerToLeftPostEQAmp(Left_mixer, 0, Left_Post_EQ_amp, 0),
  AudioConnection          patchCord_RightMixerToRightPostEQAmp(Right_mixer, 0, Right_Post_EQ_amp, 0),
  // Connect right & left input mixers to mono mix
  AudioConnection          patchCord_LeftMixerToMonoMixer(Left_mixer, Mono_mixer, 0),
  AudioConnection          patchCord_RightMixerToMonoMixer(Right_mixer, Mono_mixer, 1),
  // Connect mono mix to checkpoint
  AudioConnection          patchCord_MonoMixerToMonoPostEQAmp_Bypass(Mono_mixer, Mono_Post_EQ_amp),
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
  int8_t frequency;
  int8_t gain;
  int8_t q;

  String toString() {
    return "PEQ{" +
           "frequency=" + String(frequency) + ", " +
           "gain=" + String(gain) + ", " +
           "q=" + String(q) + ", " +
           "}";
  }
};

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
  int8_t crossoverFrequency = 50;

  // Bypass options
  bool eqEnabled = true;
  bool crossoverEnabled = true;
  bool firEnabled = true;
  bool delayEnabled = true;

  // FIR filter files (default to null)
  String firFileLeft = "";
  String firFileRight = "";
  String firFileSub = "";
  int8_t firTaps = 256;

  // Speaker delay config (default to 0)
  int8_t delayLeftMicroSeconds = 0;
  int8_t delayRightMicroSeconds = 0;
  int8_t delaySubMicroSeconds = 0;

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
}

void save_state() {
  // Save state to EEPROM
  EEPROM.put(0, state);
  Serial.println("State saved to EEPROM: " + state.toString());
}

void setEQEnabled(bool enabled) {
  state.eqEnabled = enabled;
  state.isDirty = true;

  const disableConnections = enabled ? bypassEQConnections : eqConnections;
  const enableConnections = enabled ? eqConnections : bypassEQConnections;

  //Disable connections that bypass EQ
  foreach (AudioConnection* connection, disableConnections) {
    connection->disconnect();
  }

  //Enable connections that use EQ
  foreach (AudioConnection* connection, enableConnections) {
    connection->connect();
  }
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

void setInputGains(int8_t bluetoothGain, int8_t opticalGain, int8_t generatorGain) {
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

void setSpeakerGains(int8_t leftGain, int8_t rightGain, int8_t subGain) {
  state.gainLeft = leftGain;
  state.gainRight = rightGain;
  state.gainSub = subGain;
  state.isDirty = true;

  Left_Post_Delay_amp.gain(state.gainLeft);
  Right_Post_Delay_amp.gain(state.gainRight);
  Sub_Post_Delay_amp.gain(state.gainSub);
}
  
void setEQFilters(PEQPoint filters[]) {
  state.filters = filters;
  state.isDirty = true;

  // Disconnect and delete any existing patchcords for EQ chains
  for (int i = 0; i < 16; ++i) {
    if (patchCord_LeftEQ[i]) { patchCord_LeftEQ[i]->disconnect(); delete patchCord_LeftEQ[i]; patchCord_LeftEQ[i] = nullptr; }
    if (patchCord_RightEQ[i]) { patchCord_RightEQ[i]->disconnect(); delete patchCord_RightEQ[i]; patchCord_RightEQ[i] = nullptr; }
  }

  // Count number of valid filters (assume filters[] is sized up to 15, unused have frequency <= 0)
  int numFilters = 0;
  for (int i = 0; i < 15; ++i) {
    if (filters[i].frequency > 0) ++numFilters;
    else break;
  }
  if (numFilters == 0) numFilters = 1; // Always at least 1 biquad (flat)

  // Connect Left chain: Left_mixer -> Left_EQ[0] -> ... -> Left_EQ[numFilters-1] -> Left_Post_EQ_amp
  patchCord_LeftEQ[0] = new AudioConnection(Left_mixer, Left_EQ[0]);
  for (int i = 1; i < numFilters; ++i) {
    patchCord_LeftEQ[i] = new AudioConnection(Left_EQ[i-1], Left_EQ[i]);
  }
  patchCord_LeftEQ[numFilters] = new AudioConnection(Left_EQ[numFilters-1], Left_Post_EQ_amp);

  // Connect Right chain: Right_mixer -> Right_EQ[0] -> ... -> Right_EQ[numFilters-1] -> Right_Post_EQ_amp
  patchCord_RightEQ[0] = new AudioConnection(Right_mixer, Right_EQ[0]);
  for (int i = 1; i < numFilters; ++i) {
    patchCord_RightEQ[i] = new AudioConnection(Right_EQ[i-1], Right_EQ[i]);
  }
  patchCord_RightEQ[numFilters] = new AudioConnection(Right_EQ[numFilters-1], Right_Post_EQ_amp);

  // Set biquad coefficients for each filter, unused biquads set to flat
  for (int i = 0; i < 15; ++i) {
    if (i < numFilters) {
      // Set biquad for both channels (example: peaking EQ)
      float freq = filters[i].frequency;
      float gain = filters[i].gain;
      float q = filters[i].q;
      Left_EQ[i].setPeaking(freq, q, gain);
      Right_EQ[i].setPeaking(freq, q, gain);
    } else {
      // Set unused biquads to flat
      Left_EQ[i].setPeaking(1000, 1.0, 0.0); // center freq, Q=1, gain=0dB
      Right_EQ[i].setPeaking(1000, 1.0, 0.0);
    }
  }
}


void setCrossoverFrequency(int8_t frequency) {
  state.crossoverFrequency = frequency;
  state.isDirty = true;

  // 4th-order Linkwitz-Riley crossover, by cascading two 2nd-order Butterworth stages
  Left_highpass.setFrequency(0, state.crossoverFrequency, 0.707);
  Sub_lowpass.setFrequency(0, state.crossoverFrequency, 0.707);
  Right_highpass.setFrequency(0, state.crossoverFrequency, 0.707);
  Left_highpass.setFrequency(1, state.crossoverFrequency, 0.707);
  Sub_lowpass.setFrequency(1, state.crossoverFrequency, 0.707);
  Right_highpass.setFrequency(1, state.crossoverFrequency, 0.707);
}

void setFIR(String leftFile, String rightFile, String subFile, int8_t taps) {
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


  
  
