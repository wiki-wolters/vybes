#include <Audio.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <malloc.h>
#include "FIRLoader.h"
#include "PEQProcessor.h"
#include "HeadroomMath.h"
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
#include "SdRecorder.h"
#include "SdWavPlayer.h"
#include "PeakMeter.h"
#include "SketchState.h" // OutputState/State + the sketch's exported surface
#include "AudioHold.h"
#include "FirFiles.h"
#include "TelemetryStreams.h"
#include "DelayProbe.h"
#include "RecorderControl.h"

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

// Duration of the smooth morph applied to EQ changes (ms)
#define EQ_MORPH_MS 50

// Maximum per-channel delay in microseconds. AudioEffectDelay holds the
// delayed audio in the AudioMemory pool, so an unbounded delay would
// exhaust it and kill all audio.
#define MAX_DELAY_US 20000

// FIR engine: 1 = fast convolution (FFT-based uniformly partitioned
// overlap-save; long filters at a fraction of the CPU), 0 = the original
// direct-form CMSIS FIR. Both engines produce identical, block-aligned output.
#define FIR_USE_FAST_CONVOLUTION 1

// Audio block pool size (see the AudioMemory call in setup for the budget).
// The FIR tap pool and its static arena live in FirFiles.{h,cpp}.
#define AUDIO_POOL_BLOCKS (FIR_USE_FAST_CONVOLUTION ? 480 : 240)

// RAM2 heap and audio-block-pool stats, printed where the budget matters.
// "unclaimed" is heap sbrk has never handed out; "reclaimable" is what
// mallinfo reports free inside the claimed region. Treat the sum as a floor,
// not the free heap: both numbers held still across a 196,608-byte FIR load
// (2026-08-22), so malloc's top chunk is not in either of them. The linker's
// "free for malloc/new" is the number to size anything against.
extern unsigned long _heap_end;
extern char* __brkval;
void printMemoryStats(const char* tag) {
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
// The pre-EQ amps attenuate to compensate for the EQ curve's maximum boost
// (less the spectral allowance - see HeadroomMath.h).
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

// RTA spectrum tap: a 4096-point FFT whose 1/12-octave band levels stream to
// the web UI over the ESP link. Its input is switched by updateRtaSource():
// the L+R source mix (pre-DSP) for the input scope, or a single soloed
// output's post-crossover signal so the "source" trace is band-limited
// exactly like that driver (a soloed sub then shows no energy above its
// low-pass, and the analyzer's mic/source comparison lines up instead of
// trying to correct bands the driver can't reproduce). The FFT input is
// disconnected while idle so it costs no CPU (see rtaLoop).
AudioMixer4              RTA_mixer;
RtaFFT4096               RTA_fft;

// SD recorder taps (the full mixed stereo input, pre input-EQ, so recordings
// are independent of preset EQ and master volume) and the SD WAV player,
// which feeds the aux mixers' input 2 and so plays through the whole input
// chain like any other source. Mutual exclusion (never record and play at
// once) is enforced in the handlers; both only ever touch the card from
// loop() context.
AudioRecordQueue         recordQueueL;
AudioRecordQueue         recordQueueR;
SdWavPlayer              sdPlayer;

// Stereo peak/clip meter on the input bus (drives the web UI's level bars
// via "VU" frames; see vuLoop)
PeakMeter                inputMeter;

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

// RTA tap connections (the FFT link starts disconnected; see setup).
// updateRtaSource() feeds RTA_fft from exactly one of these at a time:
// patchCord_RTAMixerToFFT for the input scope, patchCord_SoloToFFT (rebound
// to the soloed output's crossover) while an output is soloed.
AudioConnection          patchCord_LeftMixerToRTA(Left_mixer, 0, RTA_mixer, 0);
AudioConnection          patchCord_RightMixerToRTA(Right_mixer, 0, RTA_mixer, 1);
AudioConnection          patchCord_RTAMixerToFFT(RTA_mixer, 0, RTA_fft, 0);
AudioConnection          patchCord_SoloToFFT; // bound to xover[solo] on demand

// Recorder tap and player injection points
AudioConnection          patchCord_LeftMixerToRec(Left_mixer, 0, recordQueueL, 0);
AudioConnection          patchCord_RightMixerToRec(Right_mixer, 0, recordQueueR, 0);
AudioConnection          patchCord_PlayerToLeftAux(sdPlayer, 0, Left_Aux_mixer, 2);
AudioConnection          patchCord_PlayerToRightAux(sdPlayer, 1, Right_Aux_mixer, 2);

// Input bus meter taps
AudioConnection          patchCord_LeftMixerToMeter(Left_mixer, 0, inputMeter, 0);
AudioConnection          patchCord_RightMixerToMeter(Right_mixer, 0, inputMeter, 1);

SdRecorder               sdRecorder(recordQueueL, recordQueueR);

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

bool sdCardInitialized = false;
bool firFilesPending = false;

// Set by the recorder/player command handlers so recorderStatusLoop() sends
// a fresh "REC STATE" line on its next pass instead of waiting for the 1Hz
// change poll.
bool recStateDirty = false;

// Last time the recorder or player had a file open (refreshed every loop
// pass while one does, and after a delete's FAT write). sdReady() holds off
// media-presence probes for a window past this - see the comment there.
unsigned long sdLastStreamActivityMs = 0;

// --- audio hold ---
// Outputs are silent while any hold source is asserted (boot, nested config
// syncs, a queued FIR load). The state machine, its fail-safe, and the
// rationale live in AudioHold.h.
AudioHold audioHold;

// The preset/DSP state (struct definitions in SketchState.h, which also
// externs this for FirFiles.cpp)
State state;

// --- Output solo (per-output EQ measurement) ---
// Keepalive-driven like the RTA: the ESP refreshes "soloOutput <ch>" every
// couple of seconds while the analyzer measures one output, so a dropped
// connection can't leave the system stuck on one speaker. Rides the amp
// ramp via outputTargetGain (click-free) and changes nothing in the preset
// state; the soloed output keeps its normal gain/volume/mute product.
#define OUTPUT_SOLO_KEEPALIVE_TIMEOUT_MS 7000
int outputSolo = -1;
unsigned long outputSoloLastKeepaliveAt = 0;

// --- Shared output headroom pad ---
// The per-output chains have no per-channel compensation stage (a per-output
// pad would skew the balance between drivers and wreck crossover summing),
// so one shared pad - the largest net output-EQ boost across all channels -
// is folded into every source mixer's gains. "Net" folds in each channel's
// crossover response and the spectral allowance (HeadroomMath.h), so a
// boost the crossover removes, or one riding above 2kHz where program
// energy is sparse, doesn't cost the whole system headroom. Recomputed once
// per loop() pass when
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

  // Aux mixer input 2 carries the SD player (it transmits nothing while
  // idle); input 3 is unused. Both default to 1.0 and must be set
  // explicitly. The player gain is re-applied by setInputGains, so the
  // probe's restore path covers it too.
  Left_Aux_mixer.gain(2, state.gainPlayer);
  Left_Aux_mixer.gain(3, 0.0f);
  Right_Aux_mixer.gain(2, state.gainPlayer);
  Right_Aux_mixer.gain(3, 0.0f);

  // RTA tap: equal L+R mix, idle until the UI asks for it
  RTA_mixer.gain(0, 0.5);
  RTA_mixer.gain(1, 0.5);
  telemetryBegin(); // fill the RTA band-center table
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

  // Anything already in the RX buffer was sent before we rebooted and belongs
  // to the previous session - including, possibly, the tail of an interrupted
  // sync. A stale "setConfigHold 0" in there would release the boot hold
  // before the real sync has sent a single value.
  while (Serial1.available()) Serial1.read();

  // Outputs are held silent from power-on (the boot hold) until the sync
  // below completes; arm the fail-safe idle window here so it measures the
  // ESP's response to the boot event and not however long setup() spent on
  // the SD card.
  audioHold.armFailsafe(millis());

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
    if (probeIsActive()) {
      probeCleanup("PROBE ERR aborted firLoad\n");
    }
    loadFirFiles();
    firFilesPending = false;
    audioHold.setFirLoadHold(false);
  }

  // Fail-safe release: never let a missing or lost setConfigHold 0 leave the
  // device permanently silent (e.g. an ESP on firmware that predates the
  // command). See AudioHold::pollFailsafe.
  if (audioHold.pollFailsafe(router.dispatched(), millis())) {
    Serial.println("WARN: no config sync, releasing audio hold");
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
  vuLoop();
  probeLoop();
  outputSoloLoop();
  outputPadLoop();
  sdRecorder.service();
  sdPlayer.service();
  recorderStatusLoop();
}

// Clear a stale output solo once the ESP's keepalives stop arriving.
void outputSoloLoop() {
  if (outputSolo < 0) return;
  if (millis() - outputSoloLastKeepaliveAt > OUTPUT_SOLO_KEEPALIVE_TIMEOUT_MS) {
    outputSolo = -1;
    updateRtaSource(); // fall back to the L+R mix if RTA is still streaming
    Serial.println("Output solo timed out");
  }
}

// Route the FFT input to match the analyzer scope. Exactly one source feeds
// RTA_fft at a time (its input port holds one connection), so both candidate
// cords are torn down first. Call this whenever rtaStreaming() or outputSolo
// changes - not on every solo keepalive, since re-binding the cord restarts
// the FFT's accumulation window.
void updateRtaSource() {
  patchCord_RTAMixerToFFT.disconnect();
  patchCord_SoloToFFT.disconnect();
  if (!rtaStreaming()) return; // idle: leave the FFT unfed
  if (outputSolo >= 0 && outputSolo < NUM_OUTPUTS) {
    // Post-crossover, pre-PEQ - the same "pre-EQ source" semantics the input
    // scope has (it taps the source mix ahead of the input EQ), so measuring
    // with the output EQ bypassed yields the raw driver+room response.
    patchCord_SoloToFFT.connect(xover[outputSolo], 0, RTA_fft, 0);
  } else {
    patchCord_RTAMixerToFFT.connect();
  }
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
  // A config sync applies hundreds of commands one at a time, so until it
  // finishes every unsent value is still a boot default - master volume
  // 50%, output gain 0dB, input gains 1.0, crossovers bypassed. Opening an
  // output before its own commands land plays that default state, which is
  // both louder than intended and full-range. Hold every output at zero
  // until the whole picture is in place; updateAudioVolume's ramp makes the
  // release click-free.
  if (audioHold.held()) return 0.0f;
  if (probeIsActive()) {
    float gain = probeGainForOutput(ch);
    return o.invert ? -gain : gain;
  }
  // Per-output EQ measurement: everything but the soloed output is silenced;
  // the soloed one keeps its normal product so the mic measures reality.
  if (outputSolo >= 0 && ch != outputSolo) return 0.0f;
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
  // SD playback rides the same aux stage; re-applied here so probeCleanup's
  // setInputGains call restores it along with everything else
  Left_Aux_mixer.gain(2, state.gainPlayer);
  Right_Aux_mixer.gain(2, state.gainPlayer);
}

// SD playback level (linear 0..1, aux mixer input 2). Its own command
// rather than a sixth setInputGains argument because the ESP's message
// builder carries at most five parameters.
void setPlaybackGain(float gain) {
  state.gainPlayer = constrain(gain, 0.0f, 1.0f);
  Serial.println("Set playback gain: " + String(state.gainPlayer));
  Left_Aux_mixer.gain(2, state.gainPlayer);
  Right_Aux_mixer.gain(2, state.gainPlayer);
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

// Attenuate the pre-EQ amps to compensate for the maximum net boost of the
// current EQ curve (boost less the spectral allowance - HeadroomMath.h), so
// boosted bands can't clip. Unity while the EQ is bypassed ("Pure Direct" -
// no wasted headroom).
void applyPreEQGainCompensation() {
  float padDb = 0.0f;
  if (state.inputEqEnabled) {
    padDb = headroomMaxBoostDb(state.inputEqBands, MAX_PEQ_BANDS);
  }
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
    // Unity pad while off
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
  float padDb = 0.0f;
  for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
    const OutputState& o = state.outputs[ch];
    if (!o.eqEnabled) continue;
    float boost = headroomMaxBoostDb(o.peq, MAX_OUTPUT_PEQ,
                                     o.hpFreq, o.hpType, o.lpFreq, o.lpType,
                                     AUDIO_SAMPLE_RATE);
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

// sdCardInitialized only records what happened at boot. The card can be
// pulled at runtime, and SdFat then keeps answering from a stale mount: a
// removed card listed one garbage filename and failed every FIR open, with
// nothing anywhere saying the card was gone. Re-check the media on each use
// and re-mount when it comes back.
//
// EXCEPT while the recorder or player is streaming: SD.mediaPresent() probes
// the card with CMD13, SdFat's SDIO driver keeps a multi-block write open
// between the recorder's sector writes, and a CMD13 issued mid-transfer
// can't complete - status() returns 0, which mediaPresent() reads as "card
// removed" (SD.cpp). The false removal then made the next call here re-mount
// the volume UNDER the recorder's open file, killing every recording ~2s in.
// While a stream is running - or the card may still be programming just
// after one closed - trust the mount and let the stream's own read/write
// failures be the removal detector; the recorder already stops cleanly on a
// failed write.
#define SD_PROBE_HOLDOFF_MS 500
bool sdReady() {
  if (sdRecorder.isActive() || sdPlayer.isActive() ||
      (sdLastStreamActivityMs != 0 &&
       millis() - sdLastStreamActivityMs < SD_PROBE_HOLDOFF_MS)) {
    return sdCardInitialized;
  }
  const bool present = SD.mediaPresent();
  if (!present) {
    if (sdCardInitialized) {
      sdCardInitialized = false;
      Serial.println("SD card removed");
    }
    return false;
  }
  if (!sdCardInitialized) {
    sdCardInitialized = SD.begin(BUILTIN_SDCARD);
    Serial.println(sdCardInitialized ? "SD card re-mounted"
                                     : "SD card present but mount failed");
  }
  return sdCardInitialized;
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
  // The crossover response is folded into the shared headroom pad
  outputPadDirty = true;
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
  // The ESP locks preset switches and FIR edits while a recording runs, so
  // this only fires if something bypassed that lock. A FIR load stalls
  // loop() longer than the record queues can buffer - stop the recording
  // cleanly instead of shipping a glitched file.
  if (sdRecorder.isActive()) {
    sdRecorder.stop();
    Serial1.print("REC WARN stopped firload\n");
    recStateDirty = true;
  }
  firFilesPending = true;
  // The load itself happens in loop() and reads from SD, which can take
  // seconds. Stay silent across it rather than play the new preset's gains
  // through the old preset's filters.
  audioHold.setFirLoadHold(true);
}

void handleSetConfigHold(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    const bool wasHeld = audioHold.held();
    if (args[0].toInt() == 1) {
      audioHold.beginSync(millis());
    } else {
      audioHold.endSync();
    }
    if (audioHold.held() != wasHeld) {
      Serial.print("Audio hold ");
      Serial.println(audioHold.held() ? "on (config sync)" : "off (config applied)");
    }
  }
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

  if (sdReady()) {
    File root = SD.open("/");
    if (root && root.isDirectory()) {
      File file = root.openNextFile();
      while (file) {
        // Skip dotfiles. macOS writes AppleDouble sidecars ("._name") beside
        // every real file on removable media plus .Spotlight-V100/.Trashes;
        // Finder hides them, this listing did not. They doubled the list,
        // offered non-FIR blobs in the picker, cost an SD tap-count pass
        // each, and ate the ESP's fixed 1KB list cache - which drops entries
        // silently once full, making real files vanish from the UI.
        if (!file.isDirectory() && file.name()[0] != '.') {
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
  if (probeIsActive()) {
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
    outputSoloLastKeepaliveAt = millis(); // refresh on every keepalive...
    if (outputSolo != ch) {               // ...but only re-route when it moves
      outputSolo = ch;
      updateRtaSource();
    }
  } else if (outputSolo != -1) {
    outputSolo = -1;
    updateRtaSource();
  }
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
    setGrmEnabled(args[0].toInt() == 1);
  }
}

// "setVu 1" enables input level meter streaming (and acts as the keepalive
// while it repeats); "setVu 0" stops it immediately.
void handleSetVu(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    setVuEnabled(args[0].toInt() == 1);
  }
}

// "setPlaybackGain <0..1>": SD playback level into the input mix
void handleSetPlaybackGain(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount == 1) {
    setPlaybackGain(args[0].toFloat());
  }
}

// Replies with the Teensy's uptime. The ESP polls this and re-syncs the DSP
// state when uptime goes backwards (i.e. the Teensy rebooted).
void handlePing(const String& command, String* args, int argCount, OutputStream& stream) {
  char buffer[24];
  int len = snprintf(buffer, sizeof(buffer), "PONG %lu\n", (unsigned long)millis());
  stream.write(buffer, len);
}

