#include <Audio.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <malloc.h>
#include "FIRLoader.h"
#include "PEQProcessor.h"
#include "CrossoverFilter.h"
#include "MultibandCompressor.h"
#include "SerialCommandRouter.h"
#include "TeensyCommands.h"
#include "OutputStream.h"
#include "AudioFilterFIRFloat.h"
#include "IntervalTimer.h"
#include "RtaFFT4096.h"
#include "ProbeSource.h"
#include "AsyncAudioInputUSB.h"

// The .ino prototype generator injects generated prototypes for the sketch's
// functions partway down the globals below - above where OutputState is
// defined. outputTargetGain() takes an OutputState&, and a reference only
// needs the type declared, so declare it here (before the insertion point) or
// that generated prototype fails to compile.
struct OutputState;

// The .ino prototype generator injects generated prototypes for the sketch's
// functions partway down the globals below - above where OutputState is
// defined. outputTargetGain() takes an OutputState&, and a reference only
// needs the type declared, so declare it here (before the insertion point) or
// that generated prototype fails to compile.
struct OutputState;

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

// Audio block pool size (see the AudioMemory call in setup for the budget).
#define AUDIO_POOL_BLOCKS (FIR_USE_FAST_CONVOLUTION ? 480 : 240)

// RAM2 heap and audio-block-pool stats, printed where the budget matters:
// FIR loads are the only large runtime allocations, and the pool-sized
// fast-convolution buffers (~196KB) plus the loader's transient (~48KB)
// compete with the USB resampler, RTA and compressor buffers for the same
// heap. "unclaimed" is heap sbrk has never handed out; "reclaimable" is
// freed space inside the arena (usable, but possibly fragmented).
extern unsigned long _heap_end;
extern char* __brkval;
static void printMemoryStats(const char* tag) {
  struct mallinfo mi = mallinfo();
  Serial.printf("MEM %s: heap unclaimed %lu + reclaimable %lu bytes, audio blocks %d used (max %d of %d)\n",
                tag, (unsigned long)((char*)&_heap_end - __brkval),
                (unsigned long)mi.fordblks,
                AudioMemoryUsage(), AudioMemoryUsageMax(), AUDIO_POOL_BLOCKS);
}

// Audio generators
AudioSynthWaveform       Tone_generator;
AudioSynthNoisePink      pink1;
ProbeSource              probeSource; // auto delay alignment chirps

// USB input engine: 1 = AsyncAudioInputUSB (ring buffer + resampler, immune
// to host clock drift and packet burst jitter; needs the core_fork packet
// hook), 0 = the stock AudioInputUSB running on the core_fork's deepened
// receive queue. Both are stereo with identical patchcords.
#define USB_INPUT_ASYNC 1

//Audio Inputs (Bluetooth, SPDIF, USB, analog)
AudioInputI2S            Bluetooth_in;
AsyncAudioInputSPDIF3    Optical_in;
#if USB_INPUT_ASYNC
AsyncAudioInputUSB       USB_in;
#else
AudioInputUSB            USB_in;
#endif
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

// Mixed-input multiband compressor: sits after the input EQ, ahead of the
// per-output source mixers, so every output hears the same dynamics.
MultibandCompressor inputComp;

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
AudioConnection          patchCord_ProbeToGenMixer(probeSource, 0, Generator_mixer, 2);
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
AudioConnection patchCord_LeftPeqToComp(peqLeft, 0, inputComp, 0);
AudioConnection patchCord_RightPeqToComp(peqRight, 0, inputComp, 1);

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
  bool eqEnabled = true;     // PEQ bypass (bands are kept; see setOutputEqEnabled)

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

// --- Auto delay alignment probe state ---
// The chirp schedule lives in probeSource (sample-clocked, ISR context);
// everything here is loop()-context only: probeLoop() switches which output
// is soloed between chirps, and outputTargetGain() consults probeSolo. The
// solo rides the existing amp ramp, so switching is click-free. probeGain is
// applied instead of the normal gain/mute/volume product so a muted device
// or zero volume can't silence the measurement (invert is kept - the UI
// correlates on magnitude).
bool   probeActive = false;
int    probeSolo = -1;               // output the current chirp leaves through
float  probeGain = 0.0f;             // amp gain for the soloed output
int8_t probeOrder[2 * NUM_OUTPUTS];  // masked outputs ascending, then reversed
int    probeChirps = 0;
int    probeLastSlot = -1;

// --- Output solo (per-output EQ measurement) ---
// Keepalive-driven like the RTA: the ESP refreshes "soloOutput <ch>" every
// couple of seconds while the analyzer measures one output, so a dropped
// connection can't leave the system stuck on one speaker. Rides the amp
// ramp via outputTargetGain (click-free) and changes nothing in the preset
// state; the soloed output keeps its normal gain/volume/mute product.
#define OUTPUT_SOLO_KEEPALIVE_TIMEOUT_MS 7000
int outputSolo = -1;
unsigned long outputSoloLastKeepaliveAt = 0;

// --- Sweep mode (EQ tuning) ---
// Keepalive-driven like the RTA. While active, the input pre-EQ pad and the
// shared output pad are floored at SWEEP_RESERVE_DB, so dialing a boost into
// any EQ and sweeping it never moves the baseline level: the swept band
// rises above the rest of the mix (console-style) and still can't clip,
// because the reserve is spent up front instead of tracking each edit.
#define SWEEP_KEEPALIVE_TIMEOUT_MS 7000
#define SWEEP_RESERVE_DB 12.0f
bool sweepMode = false;
unsigned long sweepLastKeepaliveAt = 0;

// --- Comparison trim (A/B level matching) ---
// The ESP computes a loudness delta between the states being A/B'd and
// sends "setCompareTrim <centi-dB>" (always <= 0). The trim multiplies into
// outputTargetGain so it rides the amp ramp, and it is keepalive-guarded so
// a dropped connection can't leave the system quietly trimmed down.
#define COMPARE_KEEPALIVE_TIMEOUT_MS 7000
float compareTrimLin = 1.0f;
unsigned long compareLastKeepaliveAt = 0;

// --- Shared output headroom pad ---
// The per-output chains have no per-channel compensation stage (a per-output
// pad would skew the balance between drivers and wreck crossover summing),
// so one shared pad - the largest active output-EQ boost across all
// channels, floored at the sweep reserve while sweep mode is on - is folded
// into every source mixer's gains. Recomputed once per loop() pass when
// marked dirty, so a burst of EQ edits (the boot sync) costs one 8-channel
// curve sweep, not eighty.
float outputPadLin = 1.0f;
bool outputPadDirty = false;

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
  AudioMemory(AUDIO_POOL_BLOCKS);
  Serial.println("=== Audio Memory Debug ===");
  Serial.print("AudioMemoryUsage(): ");
  Serial.println(AudioMemoryUsage());
  Serial.print("AudioMemoryUsageMax(): ");
  Serial.println(AudioMemoryUsageMax());
  Serial.println("========================");

  // Initialize the shared input EQ
  peqLeft.begin(AUDIO_SAMPLE_RATE);
  peqRight.begin(AUDIO_SAMPLE_RATE);
  inputComp.begin(AUDIO_SAMPLE_RATE);

  // Wire and initialize the eight output chains
  for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
    busCords[ch][0].connect(inputComp, 0, sourceMixer[ch], 0);
    busCords[ch][1].connect(inputComp, 1, sourceMixer[ch], 1);
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

  // Signal generators start silent. The generator mixer's probe input (2)
  // must be zeroed explicitly - AudioMixer4 defaults every input to 1.0 and
  // the probe path only opens while a delay probe runs.
  Tone_generator.begin(0.0, 1000, WAVEFORM_SINE);
  pink1.amplitude(0.0);
  Generator_mixer.gain(0, 1.0f);
  Generator_mixer.gain(1, 1.0f);
  Generator_mixer.gain(2, 0.0f);
  Generator_mixer.gain(3, 0.0f);

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

// USB audio input health, from the core fork's usb_audio.cpp.
// feedback_accumulator is the sample rate the Teensy requests from the host
// via the isochronous feedback endpoint, in samples-per-ms * 2^24 (nominal
// 44.1 * 2^24); the stock input steers it while streaming, the async input
// leaves it nominal. Each underrun/overrun is one silent or dropped block -
// an audible glitch. The counters reset when USB reconfigures.
extern uint32_t feedback_accumulator;
extern volatile uint32_t usb_audio_underrun_count;
extern volatile uint32_t usb_audio_overrun_count;
extern volatile uint8_t usb_audio_rx_queue_count;

void loop() {
#if USB_INPUT_ASYNC
  // Rebuild the USB resampler here (not in the audio interrupt) if its
  // kill switch tripped; no-op otherwise. Prints a diagnostic when it runs.
  USB_in.healPending();
  USB_in.diagLoop();
#endif

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
    printMemoryStats("periodic");

#if USB_INPUT_ASYNC
    Serial.print("USB in (async): ");
    Serial.print(USB_in.streaming() ? "streaming" : "idle");
    Serial.print(", buffered ");
    Serial.print(USB_in.bufferedMs(), 1);
    Serial.print(" ms, step ");
    Serial.print(USB_in.stepPpm(), 1);
    Serial.print(" ppm, drops ");
    Serial.print(USB_in.drops());
    Serial.print(", starves ");
    Serial.print(USB_in.starves());
    Serial.print(", stops ");
    Serial.print(USB_in.stops());
    Serial.print(", recoveries ");
    Serial.print(USB_in.recoveries());
    Serial.print(", resyncs ");
    Serial.print(USB_in.resyncs());
    Serial.print(", allocfails ");
    Serial.print(USB_in.allocFails());
    // Races the stop detector survived (see UsbRxRing::consumerReady) and the
    // worst host packet gap this interval - 1000us is nominal, a large value
    // means the host really did stall.
    Serial.print(", falsestops ");
    Serial.print(USB_in.falseStops());
    Serial.print(", maxgap ");
    Serial.print(USB_in.takeMaxGapUs());
    Serial.println(" us");
#else
    static uint32_t lastUnderruns = 0, lastOverruns = 0;
    uint32_t underruns = usb_audio_underrun_count;
    uint32_t overruns = usb_audio_overrun_count;
    float usbHz = feedback_accumulator * (1000.0f / 16777216.0f);
    Serial.print("USB in: feedback ");
    Serial.print(usbHz, 2);
    Serial.print(" Hz (");
    Serial.print((usbHz - AUDIO_SAMPLE_RATE_EXACT) * (1e6f / AUDIO_SAMPLE_RATE_EXACT), 1);
    Serial.print(" ppm), queue ");
    Serial.print(usb_audio_rx_queue_count);
    Serial.print(" blocks, underruns +");
    Serial.print(underruns - lastUnderruns);
    Serial.print(" (total ");
    Serial.print(underruns);
    Serial.print("), overruns +");
    Serial.print(overruns - lastOverruns);
    Serial.print(" (total ");
    Serial.print(overruns);
    Serial.println(")");
    lastUnderruns = underruns;
    lastOverruns = overruns;
#endif
  }

  if (firFilesPending) {
    // A FIR load blocks loop() on SD reads and changes channel latencies -
    // either would corrupt a running measurement, so abort the probe first.
    if (probeActive) {
      probeCleanup("PROBE ERR aborted firLoad\n");
    }
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
  grmLoop();
  probeLoop();
  outputSoloLoop();
  sweepLoop();
  compareTrimLoop();
  outputPadLoop();
}

// Clear a stale output solo once the ESP's keepalives stop arriving.
void outputSoloLoop() {
  if (outputSolo < 0) return;
  if (millis() - outputSoloLastKeepaliveAt > OUTPUT_SOLO_KEEPALIVE_TIMEOUT_MS) {
    outputSolo = -1;
    Serial.println("Output solo timed out");
  }
}

// Drop sweep mode once its keepalives stop arriving.
void sweepLoop() {
  if (!sweepMode) return;
  if (millis() - sweepLastKeepaliveAt > SWEEP_KEEPALIVE_TIMEOUT_MS) {
    Serial.println("Sweep mode timed out");
    setSweepMode(false);
  }
}

// Clear a stale comparison trim once its keepalives stop arriving.
void compareTrimLoop() {
  if (compareTrimLin == 1.0f) return;
  if (millis() - compareLastKeepaliveAt > COMPARE_KEEPALIVE_TIMEOUT_MS) {
    compareTrimLin = 1.0f; // picked up by the amp ramp
    Serial.println("Compare trim timed out");
  }
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

// --- GRM (compressor gain-reduction meter) streaming ---
// Same keepalive scheme as the RTA: the ESP refreshes "setGrm 1" while a
// web client is watching the meters; streaming stops on its own otherwise.
#define GRM_FRAME_INTERVAL_MS 100
#define GRM_KEEPALIVE_TIMEOUT_MS 7000
bool grmEnabled = false;
unsigned long grmLastKeepaliveAt = 0;
unsigned long grmLastFrameAt = 0;

// While enabled, send "GRM <6 hex chars>\n" frames at ~10Hz: one byte per
// band, value = dB of reduction * 8 (0..31.9dB in 0.125dB steps).
void grmLoop() {
  if (!grmEnabled) return;
  if (millis() - grmLastKeepaliveAt > GRM_KEEPALIVE_TIMEOUT_MS) {
    grmEnabled = false;
    return;
  }
  if (millis() - grmLastFrameAt < GRM_FRAME_INTERVAL_MS) return;

  static const char HEX_DIGITS[] = "0123456789abcdef";
  char frame[4 + COMP_NUM_BANDS * 2 + 2];
  memcpy(frame, "GRM ", 4);
  size_t pos = 4;
  for (int b = 0; b < COMP_NUM_BANDS; b++) {
    int v = (int)roundf(inputComp.gainReductionDb(b) * 8.0f);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    frame[pos++] = HEX_DIGITS[v >> 4];
    frame[pos++] = HEX_DIGITS[v & 0x0F];
  }
  frame[pos++] = '\n';

  if ((size_t)Serial1.availableForWrite() < pos) return;
  Serial1.write((const uint8_t*)frame, pos);
  grmLastFrameAt = millis();
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
// While a delay probe runs, the soloed output gets the fixed probe level
// instead (see the probe state block above) and every other output is
// silenced; normal targets return through the same ramp when it ends.
static float outputTargetGain(int ch, const OutputState& o) {
  if (probeActive) {
    if (ch != probeSolo) return 0.0f;
    return o.invert ? -probeGain : probeGain;
  }
  // Per-output EQ measurement: everything but the soloed output is silenced;
  // the soloed one keeps its normal product so the mic measures reality.
  if (outputSolo >= 0 && ch != outputSolo) return 0.0f;
  if (o.mute) return 0.0f;
  float gain = powf(10.0f, o.gainDb / 20.0f) * state.targetVolume * compareTrimLin;
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
    if (slewToward(o.currentGain, outputTargetGain(ch, o), alpha)) {
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

// --- Auto delay alignment probe ---
// Protocol and chirp contract: teensy_protocol.h. PROBE lines go straight
// to the ESP link (Serial1), which relays them to the web UI as probeEvent
// websocket messages.

// Restore everything the probe touched and report why it ended. Idempotent;
// the amp targets revert through the normal ramp, so ending is click-free.
void probeCleanup(const char* message) {
  AudioNoInterrupts();
  probeSource.stop();
  AudioInterrupts();
  probeActive = false;
  probeSolo = -1;
  probeLastSlot = -1;
  // Reopen the tone/noise paths, close the probe path, and restore every
  // input-mixer gain from state (setInputGains also restores the generator
  // aux gain the probe forced to 1.0).
  Generator_mixer.gain(0, 1.0f);
  Generator_mixer.gain(1, 1.0f);
  Generator_mixer.gain(2, 0.0f);
  setInputGains(state.gainBluetooth, state.gainOptical, state.gainUSB,
                state.gainGenerator, state.gainAnalog);
  if (message) Serial1.print(message);
}

void startDelayProbe(int mask, float levelPercent) {
  // Masked outputs ascending, then the same list reversed: the UI averages
  // each output's two arrivals to cancel linear phone-clock drift.
  int forward[NUM_OUTPUTS];
  int count = 0;
  for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
    if (mask & (1 << ch)) forward[count++] = ch;
  }
  if (count == 0) {
    Serial1.print("PROBE ERR emptyMask\n");
    return;
  }
  if (probeActive) probeCleanup(nullptr); // implicit clean restart

  probeChirps = 2 * count;
  for (int i = 0; i < count; i++) {
    probeOrder[i] = (int8_t)forward[i];
    probeOrder[probeChirps - 1 - i] = (int8_t)forward[i];
  }

  // Silence the external inputs and the tone/noise generators for the
  // duration (direct mixer writes; state is untouched and restored by
  // probeCleanup), and open the probe path at unity regardless of the
  // user's generator input gain.
  Left_mixer.gain(0, 0.0f);
  Right_mixer.gain(0, 0.0f);
  Left_mixer.gain(1, 0.0f);
  Right_mixer.gain(1, 0.0f);
  Left_mixer.gain(2, 0.0f);
  Right_mixer.gain(2, 0.0f);
  Left_Aux_mixer.gain(1, 0.0f);
  Right_Aux_mixer.gain(1, 0.0f);
  Generator_mixer.gain(0, 0.0f);
  Generator_mixer.gain(1, 0.0f);
  Generator_mixer.gain(2, 1.0f);
  Left_Aux_mixer.gain(0, 1.0f);
  Right_Aux_mixer.gain(0, 1.0f);

  probeGain = constrain(levelPercent, 0.0f, 100.0f) / 100.0f;
  probeSolo = probeOrder[0];
  probeLastSlot = 0;
  probeActive = true;

  AudioNoInterrupts();
  probeSource.start((uint8_t)probeChirps, 0.5f); // -6dBFS headroom pre-amp
  AudioInterrupts();

  // An output routed with zero source gains can't emit the chirp - the UI
  // should expect a missing correlation peak rather than a probe failure.
  for (int i = 0; i < count; i++) {
    const OutputState& o = state.outputs[forward[i]];
    if (o.sourceLeft == 0.0f && o.sourceRight == 0.0f) {
      Serial1.printf("PROBE WARN unrouted %d\n", forward[i]);
    }
  }
  Serial1.printf("PROBE START %d %d %lu %lu %lu\n", mask, probeChirps,
                 (unsigned long)PROBE_PRE_ROLL_SAMPLES,
                 (unsigned long)PROBE_SPACING_SAMPLES,
                 (unsigned long)PROBE_CHIRP_SAMPLES);
}

// Track the chirp schedule from loop(): switch the soloed output at the
// midpoint of each inter-chirp gap (557ms before the chirp - the ramp fully
// settles in ~342ms, and the previous chirp ended 186ms earlier). Timing
// here is deliberately non-critical; only the chirps themselves are
// sample-exact, and they live in ProbeSource.
void probeLoop() {
  if (!probeActive) return;
  if (probeSource.isFinished()) {
    probeCleanup("PROBE DONE\n");
    return;
  }
  uint32_t s = probeSource.samplesElapsed();
  int slot = 0;
  if (s + PROBE_SPACING_SAMPLES / 2 >= PROBE_PRE_ROLL_SAMPLES) {
    slot = (int)((s + PROBE_SPACING_SAMPLES / 2 - PROBE_PRE_ROLL_SAMPLES) / PROBE_SPACING_SAMPLES);
  }
  if (slot >= probeChirps) slot = probeChirps - 1;
  if (slot != probeLastSlot) {
    probeLastSlot = slot;
    probeSolo = probeOrder[slot];
    Serial1.printf("PROBE CHIRP %d %d\n", slot, probeSolo);
  }
}

// --- Shared input EQ ---

// Attenuate the pre-EQ amps to compensate for the maximum boost of the
// current EQ curve, so boosted bands can't clip. Unity while the EQ is
// bypassed ("Pure Direct" - no wasted headroom). Sweep mode floors the pad
// at the reserve instead, so EQ edits and toggles can't move the baseline
// while a band is being swept.
void applyPreEQGainCompensation() {
  float padDb = 0.0f;
  if (state.inputEqEnabled) {
    padDb = peqLeft.calculateMaxEqBoost(state.inputEqBands, MAX_PEQ_BANDS);
  }
  if (sweepMode && padDb < SWEEP_RESERVE_DB) padDb = SWEEP_RESERVE_DB;
  peqLeft.applyPreEQGain(padDb, Left_Pre_EQ_amp, Right_Pre_EQ_amp);
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
    // Unity pad while off - unless sweep mode is holding the floor
    applyPreEQGainCompensation();
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

// One source mixer's gains: the routing values scaled by the shared pad.
void applySourceMixerGains(int ch) {
  const OutputState& o = state.outputs[ch];
  sourceMixer[ch].gain(0, o.sourceLeft * outputPadLin);
  sourceMixer[ch].gain(1, o.sourceRight * outputPadLin);
}

// Recompute the shared output pad (see the declaration for the rationale)
// and push it into every source mixer when it changed.
void refreshOutputPad() {
  outputPadDirty = false;
  float padDb = sweepMode ? SWEEP_RESERVE_DB : 0.0f;
  for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
    if (!state.outputs[ch].eqEnabled) continue;
    float boost = outputPeq[ch].calculateMaxEqBoost(state.outputs[ch].peq, MAX_OUTPUT_PEQ);
    if (boost > padDb) padDb = boost;
  }
  float padLin = (padDb > 0.0f) ? 1.0f / powf(10.0f, padDb / 20.0f) : 1.0f;
  if (padLin == outputPadLin) return;
  outputPadLin = padLin;
  for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
    applySourceMixerGains(ch);
  }
  Serial.printf("Output pad: %.1f dB headroom\n", padDb);
}

void outputPadLoop() {
  if (outputPadDirty) refreshOutputPad();
}

// Enter/leave sweep mode. Keepalive-refreshed by the ESP while a web client
// holds the mode; sweepLoop() clears it when the refreshes stop.
void setSweepMode(bool enabled) {
  sweepLastKeepaliveAt = millis();
  if (enabled == sweepMode) return;
  sweepMode = enabled;
  Serial.println(enabled ? "Sweep mode on" : "Sweep mode off");
  applyPreEQGainCompensation();
  refreshOutputPad();
}

// Morph the output's PEQ to the bands in state. animateToBands disables
// every band past MAX_OUTPUT_PEQ. Boost compensation is shared across all
// outputs (see refreshOutputPad) so relative driver levels stay intact.
void applyOutputEq(int ch) {
  outputPeq[ch].animateToBands(state.outputs[ch].peq, MAX_OUTPUT_PEQ, EQ_MORPH_MS);
  outputPadDirty = true;
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

// Pink-weighted gain of each loaded FIR filter in dB (0 = none): the mean
// power of its response sampled log-uniformly 20Hz-20kHz, i.e. its loudness
// effect on pink-ish program material. Computed once per load and reported
// as "FIRGAIN <ch> <centi-dB>" lines so the ESP's comparison mode can
// level-match FIR on/off states without ever reading the taps itself.
float firPinkGainDb[NUM_OUTPUTS] = {0.0f};

static float firPinkGain(const float* h, uint16_t n) {
  if (h == nullptr || n == 0) return 0.0f;
  const int SAMPLES = 100;
  double powerSum = 0.0;
  for (int i = 0; i < SAMPLES; i++) {
    float freq = 20.0f * powf(1000.0f, (float)i / (SAMPLES - 1)); // 20Hz..20kHz
    double w = 2.0 * M_PI * (double)freq / AUDIO_SAMPLE_RATE_EXACT;
    // DTFT via phasor recurrence; double precision keeps the rotation
    // stable over pool-sized tap counts.
    double cr = cos(w), ci = -sin(w);
    double pr = 1.0, pi = 0.0, re = 0.0, im = 0.0;
    for (uint16_t k = 0; k < n; k++) {
      re += h[k] * pr;
      im += h[k] * pi;
      double t = pr * cr - pi * ci;
      pi = pr * ci + pi * cr;
      pr = t;
    }
    powerSum += re * re + im * im;
  }
  double meanPower = powerSum / SAMPLES;
  if (meanPower < 1e-20) return -100.0f;
  return (float)(10.0 * log10(meanPower));
}

static void reportFirGains(Print& out) {
  for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
    out.printf("FIRGAIN %d %d\n", ch, (int)lroundf(firPinkGainDb[ch] * 100.0f));
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
      firPinkGainDb[ch] = 0.0f;
    }
    applyDelays();
    reportFirGains(Serial1);
    return;
  }

  // Load every channel's file, drawing taps from the shared pool. Each
  // filter is cleared before its file is (re)loaded so peak heap holds one
  // engine's buffers, not two - at the pool limit the fast-convolution
  // buffers are ~200KB and double-buffering wouldn't fit.
  printMemoryStats("before FIR loads");
  uint32_t poolUsed = 0;
  for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
    OutputState& o = state.outputs[ch];
    firFilter[ch].loadCoefficients(nullptr, 0);
    o.firTaps = 0;
    firPinkGainDb[ch] = 0.0f;
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
    if (loaded) {
      firPinkGainDb[ch] = firPinkGain(coeffs, actualTaps);
    }
    delete[] coeffs;
    if (!loaded) {
      Serial1.printf("ERROR FIR load failed: out of memory for %s (output %d)\n", o.firFile, ch);
      continue;
    }
    o.firTaps = actualTaps;
    poolUsed += actualTaps;
    Serial.printf("Output %d FIR loaded: %s (%u taps, pool %lu/%u, pink gain %.2f dB)\n",
                  ch, o.firFile, actualTaps, (unsigned long)poolUsed, FIR_TAP_POOL,
                  firPinkGainDb[ch]);
  }

  printMemoryStats("after FIR loads");

  // FIR latencies may have changed - realign the channels
  applyDelays();
  reportFirGains(Serial1);
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
  applySourceMixerGains(ch);
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

// "setOutputEqEnabled <ch> <0|1>": non-destructive bypass of one output's
// PEQ. The stored bands stay; the shared pad recomputes so only live
// boosts cost headroom.
void handleSetOutputEqEnabled(const String& command, String* args, int argCount, OutputStream& stream) {
  int ch;
  if (argCount != 2 || !parseChannel(args[0], ch)) return;
  bool enabled = args[1].toInt() == 1;
  state.outputs[ch].eqEnabled = enabled;
  outputPeq[ch].setBypass(!enabled);
  outputPadDirty = true;
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
//   <one "name size" line per file (size in bytes); WAV and TXT files carry
//    the exact FIR tap count as a third token: "name size taps">
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
          // Neither format's size can be converted to taps by the ESP (WAV
          // header size varies with metadata chunks; TXT bytes-per-tap varies
          // with the exporting tool), so report the exact count. WAV reads
          // only header bytes; TXT is one buffered pass over a tens-of-KB
          // file - listing stays fast either way.
          String name = file.name();
          bool isWav = name.endsWith(".wav") || name.endsWith(".WAV");
          bool isTxt = name.endsWith(".txt") || name.endsWith(".TXT");
          if (isWav || isTxt) {
            long taps = isWav ? FIRLoader::countWavTaps(file)
                              : FIRLoader::countTxtTaps(file);
            if (taps > 0) {
              stream.print(" ");
              stream.print(taps);
            }
          }
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

// "startDelayProbe <mask> <level>" - see teensy_protocol.h for the contract
void handleStartDelayProbe(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 2) {
    startDelayProbe(args[0].toInt() & 0xFF, args[1].toFloat());
  }
}

void handleStopDelayProbe(const String& command, String* args, int argCount, OutputStream& stream) {
  if (probeActive) {
    probeCleanup("PROBE STOP\n");
  }
}

// "setRta 1" enables RTA streaming (and acts as the keepalive while it
// repeats); "setRta 0" stops it immediately.
void handleSetRta(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    setRtaEnabled(args[0].toInt() == 1);
  }
}

// "soloOutput <ch>" silences every other output while its keepalives stay
// fresh; -1 (or any out-of-range channel) clears the solo immediately.
void handleSoloOutput(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount != 1) return;
  int ch = args[0].toInt();
  if (ch >= 0 && ch < NUM_OUTPUTS) {
    outputSolo = ch;
    outputSoloLastKeepaliveAt = millis();
  } else {
    outputSolo = -1;
  }
}

// "setSweepMode <0|1>": keepalive-refreshed while a web client holds the
// EQ sweep/tuning mode; sweepLoop() drops it when the refreshes stop.
void handleSetSweepMode(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    setSweepMode(args[0].toInt() == 1);
  }
}

// "setCompareTrim <centi-dB>": A/B level-matching trim, <= 0 (0 clears).
// Keepalive-refreshed by the ESP while comparison mode is active.
void handleSetCompareTrim(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount != 1) return;
  long centiDb = args[0].toInt();
  if (centiDb > 0) centiDb = 0;
  if (centiDb < -3000) centiDb = -3000;
  compareTrimLin = powf(10.0f, (float)centiDb / 2000.0f);
  compareLastKeepaliveAt = millis();
}

// "getFirGains": re-emit the FIRGAIN lines (e.g. after an ESP reboot, whose
// cache of them is otherwise stale until the next FIR load).
void handleGetFirGains(const String& command, String* args, int argCount, OutputStream& stream) {
  reportFirGains(stream);
}

// --- Mixed-input multiband compressor ---

void handleSetCompEnabled(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    inputComp.setEnabled(args[0].toInt() == 1);
  }
}

void handleSetCompXover(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 2) {
    inputComp.setCrossovers(args[0].toFloat(), args[1].toFloat());
  }
}

// "setCompBand <band> <thresholdDb> <ratio> <attackMs> <releaseMs> <makeupDb>"
void handleSetCompBand(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 6) {
    inputComp.setBand(args[0].toInt(), args[1].toFloat(), args[2].toFloat(),
                      args[3].toFloat(), args[4].toFloat(), args[5].toFloat());
  }
}

void handleSetCompBandBypass(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 2) {
    inputComp.setBandBypass(args[0].toInt(), args[1].toInt() == 1);
  }
}

// -1 (or any out-of-range index) clears the solo
void handleSetCompSolo(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    inputComp.setSolo(args[0].toInt());
  }
}

void handleSetCompStrength(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    inputComp.setStrength(args[0].toFloat());
  }
}

void handleSetCompVoicePriority(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    inputComp.setVoicePriority(args[0].toFloat());
  }
}

// "setGrm 1" enables gain-reduction meter streaming (and acts as the
// keepalive while it repeats); "setGrm 0" stops it immediately.
void handleSetGrm(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    grmLastKeepaliveAt = millis();
    grmEnabled = args[0].toInt() == 1;
  }
}

// Replies with the Teensy's uptime. The ESP polls this and re-syncs the DSP
// state when uptime goes backwards (i.e. the Teensy rebooted).
void handlePing(const String& command, String* args, int argCount, OutputStream& stream) {
  char buffer[24];
  int len = snprintf(buffer, sizeof(buffer), "PONG %lu\n", (unsigned long)millis());
  stream.write(buffer, len);
}
