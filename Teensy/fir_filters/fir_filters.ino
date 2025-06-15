#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

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
AudioFilterBiquad        Left_EQ;       
AudioFilterBiquad        Right_EQ;      

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
AudioFilterBiquad        Left_highpass_1;  
AudioFilterBiquad        Sub_lowpass_1;    
AudioFilterBiquad        Right_highpass_1; 
AudioFilterBiquad        Left_highpass_2;  
AudioFilterBiquad        Sub_lowpass_2;    
AudioFilterBiquad        Right_highpass_2; 

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
  AudioConnection          patchCord1(Tone_generator, 0, Generator_mixer, 0),
  AudioConnection          patchCord2(pink1, 0, Generator_mixer, 1),
  //Connect generator mixer to left and right mixers
  AudioConnection          patchCord3(Generator_mixer, Left_mixer, 3),
  AudioConnection          patchCord4(Generator_mixer, Right_mixer, 3)
};

// External input connections
AudioConnection* externalInputConnections[] = {
  AudioConnection          patchCord5(Optical_in, 0, Left_mixer, 0),
  AudioConnection          patchCord6(Optical_in, 1, Right_mixer, 0),
  // AudioConnection          patchCord7(Bluetooth_in, 0, Left_mixer, 1),
  // AudioConnection          patchCord8(Bluetooth_in, 1, Right_mixer, 1)
};

// Connect from input mixers to Post-EQ checkpoint via EQ
AudioConnection* eqConnections[] = {
  AudioConnection          patchCord9(Right_mixer, Right_EQ),
  AudioConnection          patchCord10(Left_mixer, Left_EQ),
  AudioConnection          patchCord11(Left_EQ, Left_Post_EQ_amp),
  AudioConnection          patchCord12(Right_EQ, Right_Post_EQ_amp),
  // Connect right & left EQ to mono mix
  AudioConnection          patchCord13(Left_EQ, Mono_mixer, 0),
  AudioConnection          patchCord14(Right_EQ, Mono_mixer, 1),
  // Connect mono mix to checkpoint
  AudioConnection          patchCord15(Mono_mixer, Mono_Post_EQ_amp),
};

// Connect from input mixers to Post-EQ checkpoint directly, bypassing EQ
AudioConnection* bypassEQConnections[] = {
  AudioConnection          patchCord15(Left_mixer, 0, Left_Post_EQ_amp, 0),
  AudioConnection          patchCord16(Right_mixer, 0, Right_Post_EQ_amp, 0),
  // Connect right & left input mixers to mono mix
  AudioConnection          patchCord17(Left_mixer, Mono_mixer, 0),
  AudioConnection          patchCord18(Right_mixer, Mono_mixer, 1),
  // Connect mono mix to checkpoint
  AudioConnection          patchCord19(Mono_mixer, Mono_Post_EQ_amp),
};

// Connect from Post-EQ checkpoint to Post-Crossover checkpoint via crossover
AudioConnection* crossoverConnections[] = {
  // Left
  AudioConnection          patchCord21(Left_Post_EQ_amp, Left_highpass_1),
  AudioConnection          patchCord24(Left_highpass_1, Left_highpass_2),
  AudioConnection          patchCord25(Left_highpass_2, Left_Post_Crossover_amp),
  // Right
  AudioConnection          patchCord23(Right_Post_EQ_amp, Right_highpass_1),
  AudioConnection          patchCord26(Right_highpass_1, Right_highpass_2),
  AudioConnection          patchCord27(Right_highpass_2, Right_Post_Crossover_amp),
  // Sub
  AudioConnection          patchCord20(Mono_Post_EQ_amp, Sub_lowpass_1),
  AudioConnection          patchCord22(Sub_lowpass_1, Sub_lowpass_2),
  AudioConnection          patchCord28(Sub_lowpass_2, Sub_Post_Crossover_amp),
};

// Connect from Post-EQ checkpoint to Post-Crossover checkpoint directly, bypassing crossover
AudioConnection* bypassCrossoverConnections[] = {
  AudioConnection          patchCord23(Left_Post_EQ_amp, Left_Post_Crossover_amp),
  AudioConnection          patchCord27(Right_Post_EQ_amp, Right_Post_Crossover_amp),
  AudioConnection          patchCord22(Sub_Post_EQ_amp, Sub_Post_Crossover_amp),
};

// Connect from Post-Crossover checkpoint to Post-FIR checkpoint via FIR
AudioConnection* firConnections[] = {
  AudioConnection          patchCord26(Left_Post_Crossover_amp, Left_FIR_Filter),
  AudioConnection          patchCord28(Right_Post_Crossover_amp, Right_FIR_Filter),
  AudioConnection          patchCord29(Sub_Post_Crossover_amp, Sub_FIR_Filter),
};

// Connect from Post-Crossover checkpoint to Post-FIR checkpoint directly, bypassing FIR
AudioConnection* bypassFIRConnections[] = {
  AudioConnection          patchCord26(Left_Post_Crossover_amp, Left_Post_FIR_amp),
  AudioConnection          patchCord28(Right_Post_Crossover_amp, Right_Post_FIR_amp),
  AudioConnection          patchCord29(Sub_Post_Crossover_amp, Sub_Post_FIR_amp),
};

// Connect from Post-FIR checkpoint to Post-Delay checkpoint via delay
AudioConnection* delayConnections[] = {
  AudioConnection          patchCord26(Left_Post_FIR_amp, Left_delay),
  AudioConnection          patchCord28(Right_Post_FIR_amp, Right_delay),
  AudioConnection          patchCord29(Sub_Post_FIR_amp, Sub_delay),
  AudioConnection          patchCord30(Left_delay, Left_Post_Delay_amp, 0),
  AudioConnection          patchCord31(Right_delay, Right_Post_Delay_amp, 0),
  AudioConnection          patchCord32(Sub_delay, Sub_Post_Delay_amp, 0),
};

// Connect from Post-FIR checkpoint to Post-Delay checkpoint directly, bypassing delay
AudioConnection* bypassDelayConnections[] = {
  AudioConnection          patchCord26(Left_Post_FIR_amp, Left_Post_Delay_amp),
  AudioConnection          patchCord28(Right_Post_FIR_amp, Right_Post_Delay_amp),
  AudioConnection          patchCord29(Sub_Post_FIR_amp, Sub_Post_Delay_amp),
};

//Connect from Post-delay checkpoint to outputs
AudioConnection* outputConnections[] = {
  // Analog outs
  AudioConnection          patchCord30(Left_Post_Delay_amp, L_R_Analog_Out, 0),
  AudioConnection          patchCord31(Right_Post_Delay_amp, L_R_Analog_Out, 1),
  AudioConnection          patchCord32(Sub_Post_Delay_amp, Sub_Analog_Out, 0),
  // Digital outs
  AudioConnection          patchCord33(Left_Post_Delay_amp, L_R_Spdif_Out, 0),
  AudioConnection          patchCord34(Right_Post_Delay_amp, L_R_Spdif_Out, 1),
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
}

  
  
