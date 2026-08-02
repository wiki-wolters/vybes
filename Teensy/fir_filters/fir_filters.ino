#include <Audio.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include "FIRLoader.h"
#include "PEQProcessor.h"
#include "CrossoverFilter.h"
#include "SerialCommandRouter.h"
#include "TeensyCommands.h"
#include "OutputStream.h"
#include "AudioFilterFIRFloat.h"
#include "IntervalTimer.h"
#include "RtaFFT4096.h"

// V1 8-output architecture (docs/CHANNEL_ARCHITECTURE.md): a shared stereo
// input stage (source mixing + input EQ) feeds eight identical output
// channels, each with its own source mix, HP/LP crossover, 10-band PEQ, FIR
// filter, delay and gain/invert/mute amp. The Teensy is dumb and
// per-channel: the ESP resolves crossover references and templates to
// concrete per-channel values before sending.

// Command link to the ESP: Serial1 = pins 0 (RX1) and 1 (TX1).
// See docs/WIRING.md in the repo root.
#define ESP_LINK_BAUD 115200
SerialCommandRouter router(Serial1);

// Number of output channels (octal I2S). Must match NUM_OUTPUTS on the ESP.
#define NUM_OUTPUTS 8

// PEQ bands per output channel (MAX_OUTPUT_PEQ on the ESP). The shared input
// EQ keeps MAX_PEQ_BANDS (15) from PEQProcessor.h.
#define MAX_OUTPUT_PEQ 10

// Duration of the smooth morph applied to EQ changes (ms)
#define EQ_MORPH_MS 50

#define MAX_FILENAME_LEN 64 // Maximum length for FIR filenames

// Maximum per-channel delay in microseconds. AudioEffectDelay holds the
// delayed audio in the AudioMemory pool, so an unbounded delay would
// exhaust it and kill all audio.
#define MAX_DELAY_US 20000

// FIR engine: 1 = fast convolution (FFT-based uniformly partitioned
// overlap-save; long filters at a fraction of the CPU), 0 = the original
// direct-form CMSIS FIR. Both engines produce identical, block-aligned output.
#define FIR_USE_FAST_CONVOLUTION 1

// FIR taps shared across all outputs (FIR_TAP_POOL on the ESP). Loads that
// would push the total over the pool are rejected with an error the ESP can
// relay. Fast convolution costs ~16 bytes/tap of heap; the direct engine
// runs out of CPU long before it runs out of pool.
#define FIR_TAP_POOL 12288

// Audio generators
AudioSynthWaveform       Tone_generator;
AudioSynthNoisePink      pink1;

//Audio Inputs (Bluetooth, SPDIF, USB, analog)
AudioInputI2S            Bluetooth_in;
AsyncAudioInputSPDIF3    Optical_in;
AudioInputUSB            USB_in;
// Stereo ADC (e.g. PCM1808) on I2S2: data pin 5, BCLK pin 4, LRCLK pin 3,
// MCLK pin 33. The Teensy is clock master; the ADC runs as a slave.
AudioInputI2S2           Analog_in;

// Input mixers. Left/Right_mixer channels: 0=optical, 1=bluetooth, 2=USB,
// 3=aux stage. The aux mixers carry the generator and analog inputs (the
// main mixers have no channels left), so their gains are applied there and
// main channel 3 stays at 1.0.
AudioMixer4              Left_mixer;
AudioMixer4              Right_mixer;
AudioMixer4              Left_Aux_mixer;
AudioMixer4              Right_Aux_mixer;
AudioMixer4              Generator_mixer;

// Shared input EQ (the L/R buses ahead of the per-output routing matrix).
// The pre-EQ amps attenuate to compensate for the EQ curve's maximum boost.
AudioAmplifier           Left_Pre_EQ_amp;
AudioAmplifier           Right_Pre_EQ_amp;
PEQProcessor peqLeft;
PEQProcessor peqRight;

// Per-output processing chain, one entry per output channel 0-7:
// source mixer (in 0 = L bus, in 1 = R bus) -> HP/LP crossover -> PEQ ->
// FIR -> delay -> amp (gain * volume, invert via negative gain, mute via 0).
// Bypass lives inside the objects (crossover/PEQ/FIR pass through when idle,
// delay time 0 is a passthrough) - no patchcord swapping.
AudioMixer4              sourceMixer[NUM_OUTPUTS];
CrossoverFilter          xover[NUM_OUTPUTS];
PEQProcessor             outputPeq[NUM_OUTPUTS];
AudioFilterFIRFloat      firFilter[NUM_OUTPUTS];
AudioEffectDelay         outputDelay[NUM_OUTPUTS];
AudioAmplifier           outputAmp[NUM_OUTPUTS];

// Outputs
// Analog output is octal I2S: four data lines (pins 7, 32, 6, 9) sharing the
// I2S1 clocks (BCLK=21, LRCLK=20), each feeding a stereo PCM5102A board.
// Output channels 0-7 map 1:1 onto the octal I2S channels; SPDIF mirrors
// outputs 0 and 1.
AudioOutputSPDIF3        L_R_Spdif_Out;
AudioOutputI2SOct        Analog_Out;

// RTA spectrum tap: the L+R source mix (pre-DSP) feeds a 4096-point FFT
// whose 1/12-octave band levels stream to the web UI over the ESP link.
// The FFT input is disconnected while idle so it costs no CPU (see rtaLoop).
AudioMixer4              RTA_mixer;
RtaFFT4096               RTA_fft;

// Connections (input stage - fixed, so wired at construction)

// Generator connections
AudioConnection          patchCord_GenToneToMixer(Tone_generator, 0, Generator_mixer, 0);
AudioConnection          patchCord_PinkToMixer(pink1, 0, Generator_mixer, 1);
AudioConnection          patchCord_GenMixerToLeftAux(Generator_mixer, 0, Left_Aux_mixer, 0);
AudioConnection          patchCord_GenMixerToRightAux(Generator_mixer, 0, Right_Aux_mixer, 0);

// External input connections
AudioConnection          patchCord_OpticalLToLeftMixer(Optical_in, 0, Left_mixer, 0);
AudioConnection          patchCord_OpticalRToRightMixer(Optical_in, 1, Right_mixer, 0);
AudioConnection          patchCord_BluetoothLToLeftMixer(Bluetooth_in, 0, Left_mixer, 1);
AudioConnection          patchCord_BluetoothRToRightMixer(Bluetooth_in, 1, Right_mixer, 1);
AudioConnection          patchCord_USBLToLeftMixer(USB_in, 0, Left_mixer, 2);
AudioConnection          patchCord_USBRToRightMixer(USB_in, 1, Right_mixer, 2);
AudioConnection          patchCord_AnalogLToLeftAux(Analog_in, 0, Left_Aux_mixer, 1);
AudioConnection          patchCord_AnalogRToRightAux(Analog_in, 1, Right_Aux_mixer, 1);
AudioConnection          patchCord_LeftAuxToLeftMixer(Left_Aux_mixer, 0, Left_mixer, 3);
AudioConnection          patchCord_RightAuxToRightMixer(Right_Aux_mixer, 0, Right_mixer, 3);

// RTA tap connections (the FFT link starts disconnected; see setup)
AudioConnection          patchCord_LeftMixerToRTA(Left_mixer, 0, RTA_mixer, 0);
AudioConnection          patchCord_RightMixerToRTA(Right_mixer, 0, RTA_mixer, 1);
AudioConnection          patchCord_RTAMixerToFFT(RTA_mixer, 0, RTA_fft, 0);

// Input EQ patchcords
AudioConnection patchCord_LeftMixerToPreEQ(Left_mixer, 0, Left_Pre_EQ_amp, 0);
AudioConnection patchCord_RightMixerToPreEQ(Right_mixer, 0, Right_Pre_EQ_amp, 0);
AudioConnection patchCord_LeftPreEQToPEQ(Left_Pre_EQ_amp, 0, peqLeft, 0);
AudioConnection patchCord_RightPreEQToPEQ(Right_Pre_EQ_amp, 0, peqRight, 0);

// Per-output connections, wired in setup() so they can be built in a loop
// (Teensyduino 1.54+ supports unconnected AudioConnection + connect()).
AudioConnection busCords[NUM_OUTPUTS][2];   // L/R bus -> source mixer
AudioConnection chainCords[NUM_OUTPUTS][5]; // mixer->xover->peq->fir->delay->amp
AudioConnection outCords[NUM_OUTPUTS];      // amp -> octal I2S
AudioConnection spdifCords[2];              // outputs 0/1 -> SPDIF

const int CURRENT_VERSION = 4;
bool sdCardInitialized = false;
bool firFilesPending = false;

// --- RTA (real-time analyzer) state ---
// The ESP refreshes the enable flag every couple of seconds while a web
// client is listening ("setRta 1" keepalives); streaming stops on its own
// when the keepalives stop. Bands are 1/12-octave in the base-10 sense
// (centers 10^(k/40)), 20Hz-20kHz. The web UI (WebUI/src/rta.js) uses the
// same definition and infers the resolution from the frame's band count,
// so older 31-band firmware and this 121-band version both decode.
#define RTA_NUM_BANDS 121
#define RTA_BANDS_PER_DECADE 40
#define RTA_K_LO 52 // 10^(52/40) = 20Hz
#define RTA_FRAME_INTERVAL_MS 100
#define RTA_KEEPALIVE_TIMEOUT_MS 7000
static float RTA_BAND_CENTERS[RTA_NUM_BANDS]; // filled in setup()
bool rtaEnabled = false;
unsigned long rtaLastKeepaliveAt = 0;
unsigned long rtaLastFrameAt = 0;

// Per-output channel state. Defaults are silent (source gains 0) - the ESP
// pushes the full DSP state after the "boot" event, so nothing plays from
// stale defaults.
struct OutputState {
  float sourceLeft = 0.0f;   // L bus contribution (linear gain)
  float sourceRight = 0.0f;  // R bus contribution
  float gainDb = 0.0f;       // output gain in dB (-40..+10, clamped)
  bool mute = false;         // effective mute (the ESP folds 'enabled' in)
  bool invert = false;
  int delayUs = 0;

  float hpFreq = 0.0f;       // 0 = off
  CrossoverType hpType = CROSSOVER_LR4;
  float lpFreq = 0.0f;
  CrossoverType lpType = CROSSOVER_LR4;

  PEQBand peq[MAX_OUTPUT_PEQ];

  char firFile[MAX_FILENAME_LEN] = "";
  uint16_t firTaps = 0;      // taps currently loaded (0 = none)

  float currentGain = 0.0f;  // smoothed amp gain actually applied
};

//Define a structure for holding state
struct State {
  int version = CURRENT_VERSION;

  // Input gains
  float gainBluetooth = 1.0;
  float gainOptical = 1.0;
  float gainUSB = 1.0;
  float gainGenerator = 1.0;
  float gainAnalog = 1.0;

  // Master Volume
  float volume = 0.5; // User-set volume
  float targetVolume = 0.5; // Target volume for smoothing
  bool muted = false;
  float mutePercent = 100.0; // Mute volume reduction percentage

  // Preset-level master toggles
  bool inputEqEnabled = true;
  bool firEnabled = true;
  bool delaysEnabled = true;

  OutputState outputs[NUM_OUTPUTS];

  // Shared input EQ bands (left and right run the same curve)
  PEQBand inputEqBands[MAX_PEQ_BANDS];
};

State state;

void setup() {
  Serial.begin(9600);
  Serial.println();
  Serial.println("=== Vybes DSP (8-output V1) ===");
  Serial.println("Setup starting...");

  if (CrashReport) {
    Serial.print(CrashReport);
    CrashReport.clear();
  }

  pinMode(LED_BUILTIN, OUTPUT);

  Serial.println("Initializing SD card...");
  if (SD.begin(BUILTIN_SDCARD)) {
    Serial.println("SD card initialized.");
    sdCardInitialized = true;
  } else {
    Serial.println("SD card initialization failed. Continuing without SD card.");
    sdCardInitialized = false;
  }

  // Audio connections require memory to work. The delay lines dominate: in
  // the worst case (the whole 12288-tap FIR pool on one output) the other
  // seven outputs each carry ~139ms of group-delay compensation plus the
  // 20ms user cap, ~440 blocks total. Sizing flagged for a hardware
  // benchmark in docs/FIRMWARE_V1_HANDOVER.md.
  Serial.println("Allocating audio memory");
  AudioMemory(FIR_USE_FAST_CONVOLUTION ? 480 : 240);
  Serial.println("=== Audio Memory Debug ===");
  Serial.print("AudioMemoryUsage(): ");
  Serial.println(AudioMemoryUsage());
  Serial.print("AudioMemoryUsageMax(): ");
  Serial.println(AudioMemoryUsageMax());
  Serial.println("========================");

  // Initialize the shared input EQ
  peqLeft.begin(AUDIO_SAMPLE_RATE);
  peqRight.begin(AUDIO_SAMPLE_RATE);

  // Wire and initialize the eight output chains
  for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
    busCords[ch][0].connect(peqLeft, 0, sourceMixer[ch], 0);
    busCords[ch][1].connect(peqRight, 0, sourceMixer[ch], 1);
    chainCords[ch][0].connect(sourceMixer[ch], 0, xover[ch], 0);
    chainCords[ch][1].connect(xover[ch], 0, outputPeq[ch], 0);
    chainCords[ch][2].connect(outputPeq[ch], 0, firFilter[ch], 0);
    chainCords[ch][3].connect(firFilter[ch], 0, outputDelay[ch], 0);
    chainCords[ch][4].connect(outputDelay[ch], 0, outputAmp[ch], 0);
    outCords[ch].connect(outputAmp[ch], 0, Analog_Out, ch);

    // AudioMixer4 defaults every input to gain 1.0 - zero all four
    // (2 and 3 are unused) so channels start silent until the ESP syncs
    for (int in = 0; in < 4; in++) {
      sourceMixer[ch].gain(in, 0.0f);
    }

    xover[ch].begin(AUDIO_SAMPLE_RATE);
    outputPeq[ch].begin(AUDIO_SAMPLE_RATE);
    firFilter[ch].setFastConvolution(FIR_USE_FAST_CONVOLUTION);
    outputDelay[ch].delay(0, 0.0f); // activate tap 0 (passthrough until set)
    outputAmp[ch].gain(0.0f);       // ramps up once the ESP syncs
  }
  spdifCords[0].connect(outputAmp[0], 0, L_R_Spdif_Out, 0);
  spdifCords[1].connect(outputAmp[1], 0, L_R_Spdif_Out, 1);

  // Signal generators start silent
  Tone_generator.begin(0.0, 1000, WAVEFORM_SINE);
  pink1.amplitude(0.0);

  // RTA tap: equal L+R mix, idle until the UI asks for it
  RTA_mixer.gain(0, 0.5);
  RTA_mixer.gain(1, 0.5);
  for (int b = 0; b < RTA_NUM_BANDS; b++) {
    RTA_BAND_CENTERS[b] = powf(10.0f, (float)(RTA_K_LO + b) / RTA_BANDS_PER_DECADE);
  }
  patchCord_RTAMixerToFFT.disconnect();

  // Apply the (neutral) boot state
  Serial.println("Applying state");
  setInputGains(state.gainBluetooth, state.gainOptical, state.gainUSB, state.gainGenerator, state.gainAnalog);
  setInputEqEnabled(state.inputEqEnabled);
  applyInputEqFilters(0);
  setFIREnabled(state.firEnabled);
  applyDelays();
  updateTargetVolume();

  Serial.println("=== End of Setup Memory Usage ===");
  Serial.print("AudioMemoryUsage(): ");
  Serial.println(AudioMemoryUsage());
  Serial.print("AudioMemoryUsageMax(): ");
  Serial.println(AudioMemoryUsageMax());
  Serial.println("================================");

  //Register handlers for commands arriving over the ESP serial link.
  //The command list lives in TeensyCommands.h so the test suite can verify
  //it against the commands the ESP sends.
  Serial.println("Registering command handlers");
#define VYBES_REGISTER_COMMAND(name, handler) router.on(#name, handler);
  TEENSY_COMMAND_LIST(VYBES_REGISTER_COMMAND)
#undef VYBES_REGISTER_COMMAND
  router.begin(ESP_LINK_BAUD);

  // Extra RX buffering so command bursts survive long SD-card reads.
  // (addMemoryForRead is on the concrete HardwareSerialIMXRT class, so it's
  // called here on Serial1 rather than inside the generic router.)
  static uint8_t espRxBuffer[512];
  Serial1.addMemoryForRead(espRxBuffer, sizeof(espRxBuffer));

  // Extra TX buffering: an RTA frame is 247 bytes, and the core's default
  // TX buffer (64 bytes) can't even hold one - rtaLoop would skip every
  // frame waiting for room that never appears.
  static uint8_t espTxBuffer[512];
  Serial1.addMemoryForWrite(espTxBuffer, sizeof(espTxBuffer));

  // Tell the ESP we (re)booted so it pushes the full DSP state
  router.sendEvent("boot");
}

void loop() {
  // Optional: Print some diagnostics every 20 seconds
  static unsigned long lastPrint = 0;

  if (millis() - lastPrint > 20000) {
    lastPrint = millis();
    Serial.print("Audio Processor Usage: ");
    Serial.print(AudioProcessorUsage());
    Serial.print("% (Max: ");
    Serial.print(AudioProcessorUsageMax());
    Serial.println("%)");
    AudioProcessorUsageMaxReset();
  }

  if (firFilesPending) {
    loadFirFiles();
    firFilesPending = false;
  }

  static unsigned long lastMemoryCheck = 0;
  if (millis() - lastMemoryCheck > 60000) {
    lastMemoryCheck = millis();

    Serial.print("Audio Memory Usage: ");
    Serial.println(AudioMemoryUsage());
  }
  router.loop();
  updateAudioVolume(); // Call this frequently to smooth gain changes
  rtaLoop();
}

void setRtaEnabled(bool enabled) {
  rtaLastKeepaliveAt = millis();
  if (enabled == rtaEnabled) return;
  rtaEnabled = enabled;
  Serial.println(enabled ? "RTA started" : "RTA stopped");
  if (enabled) {
    patchCord_RTAMixerToFFT.connect();
  } else {
    patchCord_RTAMixerToFFT.disconnect();
  }
}

// Sum FFT power over [lo,hi) Hz. Edge bins contribute proportionally to
// their overlap with the band, so bands narrower than one ~10.8Hz bin still
// get a sensible share instead of double-counting or reading zero.
static float rtaBandPower(float lo, float hi) {
  const float binWidth = RtaFFT4096::binWidthHz();
  int first = (int)roundf(lo / binWidth);
  int last = (int)roundf(hi / binWidth);
  if (first < 1) first = 1; // skip the DC bin
  if (last > RtaFFT4096::NUM_BINS - 1) last = RtaFFT4096::NUM_BINS - 1;
  float power = 0.0f;
  for (int i = first; i <= last; i++) {
    float overlap = min(hi, (i + 0.5f) * binWidth) - max(lo, (i - 0.5f) * binWidth);
    if (overlap <= 0.0f) continue;
    power += RTA_fft.readPower(i) * (overlap / binWidth);
  }
  return power;
}

// While enabled, send "RTA <242 hex chars>\n" frames at ~10Hz: one byte per
// band, value = (dB + 100) * 2, i.e. -100dB..+27.5dB in 0.5dB steps. A frame
// is 247 bytes - the ESP's RX line buffer (RX_LINE_MAX in teensy_comm.cpp)
// and the Serial1 TX buffer (espTxBuffer in setup) are both sized for it.
void rtaLoop() {
  if (!rtaEnabled) return;
  if (millis() - rtaLastKeepaliveAt > RTA_KEEPALIVE_TIMEOUT_MS) {
    setRtaEnabled(false);
    return;
  }
  if (millis() - rtaLastFrameAt < RTA_FRAME_INTERVAL_MS) return;
  if (!RTA_fft.available()) return;
  RTA_fft.analyze();

  static const char HEX_DIGITS[] = "0123456789abcdef";
  char frame[4 + RTA_NUM_BANDS * 2 + 1];
  memcpy(frame, "RTA ", 4);
  size_t pos = 4;
  // Band edges are a twelfth of an octave apart: center * 10^(+/-1/80)
  for (int b = 0; b < RTA_NUM_BANDS; b++) {
    float power = rtaBandPower(RTA_BAND_CENTERS[b] * 0.971628f,
                               RTA_BAND_CENTERS[b] * 1.029200f);
    float dB = (power > 1e-10f) ? 10.0f * log10f(power) : -100.0f;
    int v = (int)roundf((dB + 100.0f) * 2.0f);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    frame[pos++] = HEX_DIGITS[v >> 4];
    frame[pos++] = HEX_DIGITS[v & 0x0F];
  }
  frame[pos++] = '\n';

  // Never block on the UART; skip the frame if the TX buffer is busy
  if ((size_t)Serial1.availableForWrite() < pos) return;
  Serial1.write((const uint8_t*)frame, pos);
  rtaLastFrameAt = millis();
}

// Move 'current' toward 'target' with an exponential ramp whose speed is
// independent of how fast loop() runs. Returns true if the value changed.
static bool slewToward(float& current, float target, float alpha) {
  const float MIN_CHANGE = 0.001f;
  if (fabsf(target - current) > MIN_CHANGE) {
    current += (target - current) * alpha;
    return true;
  }
  if (current != target) {
    current = target;
    return true;
  }
  return false;
}

// The gain an output's amp should settle at: output gain (dB) * master
// volume, negated for invert, zero when muted. Smoothing rides the whole
// product, so volume, gain, mute and invert changes are all click-free.
static float outputTargetGain(const OutputState& o) {
  if (o.mute) return 0.0f;
  float gain = powf(10.0f, o.gainDb / 20.0f) * state.targetVolume;
  return o.invert ? -gain : gain;
}

// Function to smoothly update the per-output amp gains
void updateAudioVolume() {
  // Time constant of the ramp: ~63% of the way in RAMP_TAU_MS, settled in
  // roughly 3x that. Time-based so SD reads etc. don't change the ramp speed.
  const float RAMP_TAU_MS = 60.0f;

  static unsigned long lastUpdate = 0;
  unsigned long now = millis();
  float dt = (float)(now - lastUpdate);
  lastUpdate = now;
  if (dt <= 0) return;
  float alpha = dt / RAMP_TAU_MS;
  if (alpha > 1.0f) alpha = 1.0f;

  for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
    OutputState& o = state.outputs[ch];
    if (slewToward(o.currentGain, outputTargetGain(o), alpha)) {
      outputAmp[ch].gain(o.currentGain);
    }
  }
}

void updateTargetVolume() {
  if (state.muted) {
    float reduction = state.mutePercent / 100.0;
    state.targetVolume = state.volume * (1.0 - reduction);
  } else {
    state.targetVolume = state.volume;
  }
}

void setMute(bool mute) {
  if (mute == state.muted) return; // No change
  state.muted = mute;
  updateTargetVolume();
  Serial.println(state.muted ? "Muting audio" : "Unmuting audio");
}

void setMutePercent(float percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  state.mutePercent = percent;
  updateTargetVolume(); // Recalculate target volume if muted
  Serial.println("Set mute percent: " + String(percent));
}

void setVolume(float volume) {
  // Apply a cubic curve to the volume for a more natural logarithmic response
  // The input 'volume' is linear 0.0-1.0
  float logVolume = volume * volume * volume;

  Serial.println("Set volume: " + String(volume) + " (log: " + String(logVolume) + ")");
  state.volume = logVolume;
  updateTargetVolume();
  // Do NOT apply gain directly here. It will be smoothed in updateAudioVolume().
}

void setInputGains(float bluetoothGain, float opticalGain, float usbGain, float generatorGain, float analogGain) {
  Serial.println("Set input gains: bluetooth " + String(bluetoothGain) + ", optical " + String(opticalGain) + ", usb " + String(usbGain) + ", generator " + String(generatorGain) + ", analog " + String(analogGain));
  state.gainBluetooth = bluetoothGain;
  state.gainOptical = opticalGain;
  state.gainUSB = usbGain;
  state.gainGenerator = generatorGain;
  state.gainAnalog = analogGain;
  Left_mixer.gain(0, state.gainOptical);
  Right_mixer.gain(0, state.gainOptical);
  Left_mixer.gain(1, state.gainBluetooth);
  Right_mixer.gain(1, state.gainBluetooth);
  Left_mixer.gain(2, state.gainUSB);
  Right_mixer.gain(2, state.gainUSB);
  // Generator and analog gains are applied in the aux stage feeding channel 3
  Left_mixer.gain(3, 1.0);
  Right_mixer.gain(3, 1.0);
  Left_Aux_mixer.gain(0, state.gainGenerator);
  Right_Aux_mixer.gain(0, state.gainGenerator);
  Left_Aux_mixer.gain(1, state.gainAnalog);
  Right_Aux_mixer.gain(1, state.gainAnalog);
}

void setTone(float frequency, float volumePercent) {
  Serial.println("Set tone: " + String(frequency) + " Hz at " + String(volumePercent) + "%");
  Tone_generator.frequency(frequency);
  Tone_generator.amplitude(volumePercent / 100.0f);
}

void stopTone() {
  Serial.println("Stop tone");
  Tone_generator.amplitude(0.0f);
}

void setNoise(float volumePercent) {
  Serial.println("Set pink noise: " + String(volumePercent) + "%");
  pink1.amplitude(volumePercent / 100.0f);
}

// --- Shared input EQ ---

// Attenuate the pre-EQ amps to compensate for the maximum boost of the
// current EQ curve, so boosted bands can't clip.
void applyPreEQGainCompensation() {
  float maxBoost = peqLeft.calculateMaxEqBoost(state.inputEqBands, MAX_PEQ_BANDS);
  peqLeft.applyPreEQGain(maxBoost, Left_Pre_EQ_amp, Right_Pre_EQ_amp);
}

// Apply all bands in state.inputEqBands to both PEQ processors. Disabled
// bands are passed through too - the processors bypass them individually.
void applyInputEqFilters(unsigned long animationDurationMs) {
  peqLeft.animateToBands(state.inputEqBands, MAX_PEQ_BANDS, animationDurationMs);
  peqRight.animateToBands(state.inputEqBands, MAX_PEQ_BANDS, animationDurationMs);
  applyPreEQGainCompensation();
}

void setInputEqEnabled(bool enabled) {
  Serial.println(String("Set input EQ enabled: ") + (enabled ? "yes" : "no"));
  state.inputEqEnabled = enabled;
  peqLeft.setBypass(!enabled);
  peqRight.setBypass(!enabled);

  if (enabled) {
    // EQ is enabled, so apply the filters and the gain compensation
    applyInputEqFilters(EQ_MORPH_MS);
  } else {
    // EQ is disabled, so set the pre-amp gain to 1.0
    Left_Pre_EQ_amp.gain(1.0);
    Right_Pre_EQ_amp.gain(1.0);
  }
}

void resetInputEqBands(int fromIndex) {
  if (fromIndex < 0) fromIndex = 0;
  for (int i = fromIndex; i < MAX_PEQ_BANDS; i++) {
    state.inputEqBands[i].enabled = false;
    state.inputEqBands[i].frequency = 1000.0f;
    state.inputEqBands[i].gain = 0.0f;
    state.inputEqBands[i].q = 1.0f;
  }
  applyInputEqFilters(EQ_MORPH_MS);
}

// --- Per-output DSP ---

// Morph the output's PEQ to the bands in state. animateToBands disables
// every band past MAX_OUTPUT_PEQ. Unlike the input EQ there is no boost
// compensation stage - output gain staging is explicit in the channel strip.
void applyOutputEq(int ch) {
  outputPeq[ch].animateToBands(state.outputs[ch].peq, MAX_OUTPUT_PEQ, EQ_MORPH_MS);
}

void setFIREnabled(bool enabled) {
  Serial.println(String("Set fir enabled: ") + (enabled ? "yes" : "no"));
  state.firEnabled = enabled;

  for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
    firFilter[ch].setEnabled(enabled);
  }

  // FIR latency compensation only applies while the filters are active
  applyDelays();
}

void setDelaysEnabled(bool enabled) {
  Serial.println(String("Set delays enabled: ") + (enabled ? "yes" : "no"));
  state.delaysEnabled = enabled;
  applyDelays();
}

// Group delay of a linear-phase FIR filter in microseconds: (N-1)/2 samples.
// Both FIR engines produce block-aligned output, so no engine-specific
// processing latency needs to be added here.
// (For minimum-phase FIR files this over-compensates; see docs/WIRING.md.)
static float firGroupDelayUs(uint16_t taps) {
  if (taps == 0) return 0.0f;
  return ((taps - 1) / 2.0f) * (1000000.0f / AUDIO_SAMPLE_RATE_EXACT);
}

// Apply user delays plus automatic FIR latency alignment: every output is
// padded so all eight share the latency of the slowest FIR filter. The
// alignment stays active when user delays are toggled off - it corrects an
// artifact of the FIR filters, it isn't a user delay.
void applyDelays() {
  float maxLat = 0.0f;
  if (state.firEnabled) {
    for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
      float lat = firGroupDelayUs(state.outputs[ch].firTaps);
      if (lat > maxLat) maxLat = lat;
    }
  }
  for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
    float comp = state.firEnabled ? maxLat - firGroupDelayUs(state.outputs[ch].firTaps) : 0.0f;
    float user = state.delaysEnabled ? (float)state.outputs[ch].delayUs : 0.0f;
    outputDelay[ch].delay(0, (user + comp) / 1000.0f); // milliseconds
  }
  if (maxLat > 0.0f) {
    Serial.printf("FIR latency alignment: %.0f us\n", maxLat);
  }
}

void loadFirFiles() {
  // Note: incoming serial commands are buffered by the UART while we read
  // from the SD card, so no special handling is needed here.
  if (!sdCardInitialized) {
    Serial.println("SD not initialized - can't load FIR files");
    // Clear any existing FIR filters to ensure no stale filters are used
    for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
      firFilter[ch].loadCoefficients(nullptr, 0);
      state.outputs[ch].firTaps = 0;
    }
    applyDelays();
    return;
  }

  // Load every channel's file, drawing taps from the shared pool. Each
  // filter is cleared before its file is (re)loaded so peak heap holds one
  // engine's buffers, not two - at the pool limit the fast-convolution
  // buffers are ~200KB and double-buffering wouldn't fit.
  uint32_t poolUsed = 0;
  for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
    OutputState& o = state.outputs[ch];
    firFilter[ch].loadCoefficients(nullptr, 0);
    o.firTaps = 0;
    if (o.firFile[0] == '\0') continue;

    uint32_t remaining = FIR_TAP_POOL - poolUsed;
    if (remaining == 0) {
      Serial1.printf("ERROR FIR pool exhausted, skipping %s (output %d)\n", o.firFile, ch);
      continue;
    }

    // truncateToMax=false: a file that doesn't fit the remaining pool is
    // rejected outright (actualTaps then reports the requested size)
    uint16_t actualTaps = 0;
    float* coeffs = FIRLoader::loadCoefficients(o.firFile, actualTaps, (uint16_t)remaining, false);
    if (!coeffs) {
      if (actualTaps > 0) {
        Serial1.printf("ERROR FIR pool exceeded: %s needs %u taps, %lu of %u left (output %d)\n",
                       o.firFile, actualTaps, (unsigned long)remaining, FIR_TAP_POOL, ch);
      } else {
        Serial1.printf("ERROR FIR load failed: %s (output %d)\n", o.firFile, ch);
      }
      continue;
    }

    bool loaded = firFilter[ch].loadCoefficients(coeffs, actualTaps);
    delete[] coeffs;
    if (!loaded) {
      Serial1.printf("ERROR FIR load failed: out of memory for %s (output %d)\n", o.firFile, ch);
      continue;
    }
    o.firTaps = actualTaps;
    poolUsed += actualTaps;
    Serial.printf("Output %d FIR loaded: %s (%u taps, pool %lu/%u)\n",
                  ch, o.firFile, actualTaps, (unsigned long)poolUsed, FIR_TAP_POOL);
  }

  // FIR latencies may have changed - realign the channels
  applyDelays();
}

/*
 * Define command handlers (invoked by the serial command router)
 */

// Parse and bounds-check the output channel argument common to every
// setOutput* command. Returns false (after logging) for anything invalid.
static bool parseChannel(const String& arg, int& ch) {
  ch = arg.toInt();
  if (ch < 0 || ch >= NUM_OUTPUTS || (ch == 0 && arg != "0")) {
    Serial.println("Invalid output channel: " + arg);
    return false;
  }
  return true;
}

void handleSetOutputGain(const String& command, String* args, int argCount, OutputStream& stream) {
  int ch;
  if (argCount != 2 || !parseChannel(args[0], ch)) return;
  state.outputs[ch].gainDb = constrain(args[1].toFloat(), -40.0f, 10.0f);
  // Applied by updateAudioVolume() so the change ramps click-free
}

void handleSetOutputMute(const String& command, String* args, int argCount, OutputStream& stream) {
  int ch;
  if (argCount != 2 || !parseChannel(args[0], ch)) return;
  state.outputs[ch].mute = args[1].toInt() == 1;
}

void handleSetOutputInvert(const String& command, String* args, int argCount, OutputStream& stream) {
  int ch;
  if (argCount != 2 || !parseChannel(args[0], ch)) return;
  state.outputs[ch].invert = args[1].toInt() == 1;
}

void handleSetOutputSource(const String& command, String* args, int argCount, OutputStream& stream) {
  int ch;
  if (argCount != 3 || !parseChannel(args[0], ch)) return;
  OutputState& o = state.outputs[ch];
  o.sourceLeft = args[1].toFloat();
  o.sourceRight = args[2].toFloat();
  sourceMixer[ch].gain(0, o.sourceLeft);
  sourceMixer[ch].gain(1, o.sourceRight);
}

void handleSetOutputDelay(const String& command, String* args, int argCount, OutputStream& stream) {
  int ch;
  if (argCount != 2 || !parseChannel(args[0], ch)) return;
  // Clamp rather than reject: delays also arrive during the boot sync, and a
  // bad value must never take the audio down (see MAX_DELAY_US).
  state.outputs[ch].delayUs = constrain(args[1].toInt(), 0L, (long)MAX_DELAY_US);
  applyDelays();
}

// Shared by the setOutputHp/setOutputLp handlers: parse "<freq> <type>"
// (freq 0 = section off) into the given state fields and reconfigure.
static void handleOutputFilter(String* args, int argCount, bool isHighpass) {
  int ch;
  if (argCount != 3 || !parseChannel(args[0], ch)) return;
  float freq = args[1].toFloat();
  CrossoverType type;
  if (!xoverParseType(args[2].c_str(), type)) {
    Serial.println("Invalid crossover type: " + args[2]);
    return;
  }
  OutputState& o = state.outputs[ch];
  if (isHighpass) {
    o.hpFreq = freq;
    o.hpType = type;
    xover[ch].setHighpass(freq, type);
  } else {
    o.lpFreq = freq;
    o.lpType = type;
    xover[ch].setLowpass(freq, type);
  }
}

void handleSetOutputHp(const String& command, String* args, int argCount, OutputStream& stream) {
  handleOutputFilter(args, argCount, true);
}

void handleSetOutputLp(const String& command, String* args, int argCount, OutputStream& stream) {
  handleOutputFilter(args, argCount, false);
}

void handleSetOutputEq(const String& command, String* args, int argCount, OutputStream& stream) {
  int ch;
  if (argCount != 5 || !parseChannel(args[0], ch)) return;
  int band = args[1].toInt();
  if (band < 0 || band >= MAX_OUTPUT_PEQ) return;

  float frequency = args[2].toFloat();
  float q = args[3].toFloat();
  float gain = args[4].toFloat();

  // A frequency of 0 disables the band (same convention as the input EQ)
  PEQBand& b = state.outputs[ch].peq[band];
  b.enabled = frequency > 0.0f;
  b.frequency = frequency;
  b.q = q;
  b.gain = gain;

  applyOutputEq(ch);
}

void handleResetOutputEq(const String& command, String* args, int argCount, OutputStream& stream) {
  int ch;
  if (argCount != 2 || !parseChannel(args[0], ch)) return;
  int fromIndex = args[1].toInt();
  if (fromIndex < 0) fromIndex = 0;
  for (int i = fromIndex; i < MAX_OUTPUT_PEQ; i++) {
    state.outputs[ch].peq[i] = {1000.0f, 0.0f, 1.0f, false};
  }
  applyOutputEq(ch);
}

void handleSetInputEq(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 4) {
    int index = args[0].toInt();
    float frequency = args[1].toFloat();
    float q = args[2].toFloat();
    float gain = args[3].toFloat();

    if (index >= 0 && index < MAX_PEQ_BANDS) {
      // A frequency of 0 (i.e. "setInputEq n 0 0 0") disables the band
      state.inputEqBands[index].enabled = frequency > 0.0f;
      state.inputEqBands[index].frequency = frequency;
      state.inputEqBands[index].q = q;
      state.inputEqBands[index].gain = gain;

      // Morph smoothly to the new curve
      applyInputEqFilters(EQ_MORPH_MS);
    }
  }
}

void handleResetInputEq(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    resetInputEqBands(args[0].toInt());
  }
}

void handleSetInputEqEnabled(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    setInputEqEnabled(args[0].toInt() == 1);
  }
}

void handleSetFIR(const String& command, String* args, int argCount, OutputStream& stream) {
  int ch;
  if (argCount == 2) { // "setFir <ch> <file>"
    if (!parseChannel(args[0], ch)) return;
    strncpy(state.outputs[ch].firFile, args[1].c_str(), MAX_FILENAME_LEN - 1);
    state.outputs[ch].firFile[MAX_FILENAME_LEN - 1] = '\0';
  } else if (argCount == 1) { // bare "setFir <ch>" clears
    if (!parseChannel(args[0], ch)) return;
    state.outputs[ch].firFile[0] = '\0';
  }
  // Files are read when loadFirFiles arrives, not here
}

void handleSetFIREnabled(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    setFIREnabled(args[0].toInt() == 1);
  }
}

void handleLoadFirFiles(const String& command, String* args, int argCount, OutputStream& stream) {
  firFilesPending = true;
}

void handleSetDelaysEnabled(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    setDelaysEnabled(args[0].toInt() == 1);
  }
}

// Legacy remote/button path: global left/right/sub gains have no place in
// the 8-output model (per-output gains replaced them), but the ESP still
// sends the command until its remote code is reworked - accept and ignore.
void handleSetSpeakerGains(const String& command, String* args, int argCount, OutputStream& stream) {
  Serial.println("Ignoring legacy setSpeakerGains");
}

void handleSetMute(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    setMute(args[0].toInt() == 1);
  }
}

void handleSetMutePercent(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    setMutePercent(args[0].toFloat());
  }
}

void handleSetVolume(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    setVolume(args[0].toFloat());
  }
}

// Replies with the SD file list, framed as:
//   FILES
//   <one "name size" line per file, size in bytes>
//   EOT
void handleGetFiles(const String& command, String* args, int argCount, OutputStream& stream) {
  stream.print("FILES\n");

  if (sdCardInitialized) {
    File root = SD.open("/");
    if (root && root.isDirectory()) {
      File file = root.openNextFile();
      while (file) {
        if (!file.isDirectory()) {
          stream.print(file.name());
          stream.print(" ");
          stream.print((unsigned long)file.size());
          stream.print("\n");
        }
        file.close();
        file = root.openNextFile();
      }
    }
    if (root) root.close();
  } else {
    Serial.println("getFiles: SD not initialized");
  }

  stream.print("EOT\n");
}

void handleSetInputGains(const String& command, String* args, int argCount, OutputStream& stream) {
  // 4-arg form predates the analog input; keep accepting it
  if (argCount == 5) {
    setInputGains(args[0].toFloat(), args[1].toFloat(), args[2].toFloat(), args[3].toFloat(), args[4].toFloat());
  } else if (argCount == 4) {
    setInputGains(args[0].toFloat(), args[1].toFloat(), args[2].toFloat(), args[3].toFloat(), state.gainAnalog);
  }
}

void handleSetTone(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 2) {
    setTone(args[0].toFloat(), args[1].toFloat());
  }
}

void handleStopTone(const String& command, String* args, int argCount, OutputStream& stream) {
  stopTone();
}

void handleSetNoise(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    setNoise(args[0].toFloat());
  }
}

// "setRta 1" enables RTA streaming (and acts as the keepalive while it
// repeats); "setRta 0" stops it immediately.
void handleSetRta(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    setRtaEnabled(args[0].toInt() == 1);
  }
}

// Replies with the Teensy's uptime. The ESP polls this and re-syncs the DSP
// state when uptime goes backwards (i.e. the Teensy rebooted).
void handlePing(const String& command, String* args, int argCount, OutputStream& stream) {
  char buffer[24];
  int len = snprintf(buffer, sizeof(buffer), "PONG %lu\n", (unsigned long)millis());
  stream.write(buffer, len);
}
