#include "DelayProbe.h"

#include <Arduino.h>
#include <Audio.h> // AudioNoInterrupts/AudioInterrupts
#include <string.h>

#include "SketchState.h"
#include "teensy_protocol.h" // PROBE_*/SWEEP_* chirp/schedule contract

// probeLoop() switches which output is soloed between chirps, and the
// sketch's outputTargetGain() consults probeGainForOutput(). The solo rides
// the existing amp ramp, so switching is click-free. probeGain is applied
// instead of the normal gain/mute/volume product so a muted device or zero
// volume can't silence the measurement.
//
// The delay probe and the measurement sweep share this one active session:
// only one of the two can run at a time (both use the single ProbeSource
// object), so probeSolo/probeGain/probeLastSlot are common scratch for
// whichever kind is active, while each kind keeps its own slot-order array
// and derived schedule (probeOrder/probeChirps for the delay probe,
// sweepOrder/sweepChirps for the sweep - the sweep's spacing is derived from
// its chirpSamples, so it can't reuse the delay probe's fixed constants).
static bool   probeActive = false;
static bool   sweepMode = false;            // false = delay probe, true = sweep
static int    probeSolo = -1;                // output the current chirp leaves through
static float  probeGain = 0.0f;              // amp gain for the soloed output
static int    probeLastSlot = -1;

static int8_t probeOrder[2 * NUM_OUTPUTS];   // masked outputs ascending, then reversed
static int    probeChirps = 0;

static int8_t sweepOrder[NUM_OUTPUTS * SWEEP_MAX_PASSES]; // ascending, repeated nPasses times
static int    sweepChirps = 0;

// The active session's own schedule, so probeLoop()'s slot arithmetic works
// for either kind without hardcoding the delay probe's fixed constants.
static uint32_t activePreRoll = PROBE_PRE_ROLL_SAMPLES;
static uint32_t activeSpacing = PROBE_SPACING_SAMPLES;
static int      activeChirps = 0;

bool probeIsActive() {
  return probeActive;
}

float probeGainForOutput(int ch) {
  return (ch == probeSolo) ? probeGain : 0.0f;
}

// Silence the external inputs and the tone/noise generators for the
// duration (direct mixer writes; state is untouched and restored by
// probeCleanup), and open the probe path at unity regardless of the user's
// generator input gain. Shared setup for both kinds of session.
static void openProbePath() {
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
}

// SD playback rides the aux mixer's input 2, which the probe's input muting
// leaves open - stop it before either kind of session starts.
static void stopSdPlaybackForProbe() {
  if (sdPlayer.isActive()) {
    sdPlayer.stop();
    recStateDirty = true;
  }
}

void probeCleanup(const char* message) {
  AudioNoInterrupts();
  probeSource.stop();
  AudioInterrupts();
  const bool wasSweep = sweepMode;
  probeActive = false;
  sweepMode = false;
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
  if (!message) return;
  // Existing callers (fir_filters.ino) pass a literal "PROBE ..." string -
  // swap the prefix when a sweep was actually what just ended, so they
  // don't have to know which kind of session they're cleaning up.
  if (wasSweep && strncmp(message, "PROBE ", 6) == 0) {
    Serial1.print("SWEEP ");
    Serial1.print(message + 6);
  } else {
    Serial1.print(message);
  }
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

  stopSdPlaybackForProbe();

  probeChirps = 2 * count;
  for (int i = 0; i < count; i++) {
    probeOrder[i] = (int8_t)forward[i];
    probeOrder[probeChirps - 1 - i] = (int8_t)forward[i];
  }

  openProbePath();

  probeGain = constrain(levelPercent, 0.0f, 100.0f) / 100.0f;
  probeSolo = probeOrder[0];
  probeLastSlot = 0;
  probeActive = true;
  sweepMode = false;
  activePreRoll = PROBE_PRE_ROLL_SAMPLES;
  activeSpacing = PROBE_SPACING_SAMPLES;
  activeChirps = probeChirps;

  AudioNoInterrupts();
  // -6dBFS headroom pre-amp; explicit PROBE_* constants keep this call
  // byte-for-byte identical to before ProbeSource's parameters became
  // runtime-configurable for the measurement sweep.
  probeSource.start((uint8_t)probeChirps, 0.5f, PROBE_F0_HZ, PROBE_F1_HZ,
                    PROBE_CHIRP_SAMPLES, PROBE_FADE_SAMPLES,
                    PROBE_PRE_ROLL_SAMPLES, PROBE_SPACING_SAMPLES);
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

// "startSweepProbe <mask> <level%> <f0> <f1> <chirpSamples> <nPasses>" - see
// teensy_protocol.h. Slot order is ascending outputs repeated nPasses times
// (not reversed) - a pass-to-pass drift/consistency check on the SAME
// output, unlike the delay probe's drift-cancelling forward/reverse pair.
void startSweepProbe(int mask, float levelPercent, double f0Hz, double f1Hz,
                     uint32_t chirpSamples, int nPasses) {
  int forward[NUM_OUTPUTS];
  int count = 0;
  for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
    if (mask & (1 << ch)) forward[count++] = ch;
  }
  if (count == 0) {
    Serial1.print("SWEEP ERR emptyMask\n");
    return;
  }
  // Bounds so the fixed-size sweepOrder[] can't overrun and the ratio_
  // computation in ProbeSource can't divide by zero / take log of a
  // non-positive number.
  const bool validRange = (nPasses > 0 && nPasses <= SWEEP_MAX_PASSES) &&
                          (f0Hz > 0.0 && f1Hz > f0Hz) &&
                          (chirpSamples > (uint32_t)(2 * PROBE_FADE_SAMPLES) &&
                           chirpSamples <= 10UL * SWEEP_DEFAULT_CHIRP_SAMPLES);
  if (!validRange) {
    Serial1.print("SWEEP ERR badParam\n");
    return;
  }

  if (probeActive) probeCleanup(nullptr); // implicit clean restart (either kind)

  stopSdPlaybackForProbe();

  sweepChirps = count * nPasses;
  for (int p = 0; p < nPasses; p++) {
    for (int i = 0; i < count; i++) {
      sweepOrder[p * count + i] = (int8_t)forward[i];
    }
  }

  openProbePath();

  const uint32_t fade = PROBE_FADE_SAMPLES;
  const uint32_t preRoll = PROBE_PRE_ROLL_SAMPLES;
  const uint32_t spacing = chirpSamples + SWEEP_MIN_TAIL_SAMPLES;

  probeGain = constrain(levelPercent, 0.0f, 100.0f) / 100.0f;
  probeSolo = sweepOrder[0];
  probeLastSlot = 0;
  probeActive = true;
  sweepMode = true;
  activePreRoll = preRoll;
  activeSpacing = spacing;
  activeChirps = sweepChirps;

  AudioNoInterrupts();
  probeSource.start((uint8_t)sweepChirps, 0.5f, f0Hz, f1Hz, chirpSamples, fade,
                    preRoll, spacing);
  AudioInterrupts();

  for (int i = 0; i < count; i++) {
    const OutputState& o = state.outputs[forward[i]];
    if (o.sourceLeft == 0.0f && o.sourceRight == 0.0f) {
      Serial1.printf("SWEEP WARN unrouted %d\n", forward[i]);
    }
  }
  Serial1.printf("SWEEP START %d %d %lu %lu %lu %.2f %.2f %lu\n", mask, nPasses,
                 (unsigned long)preRoll, (unsigned long)spacing,
                 (unsigned long)chirpSamples, f0Hz, f1Hz, (unsigned long)fade);
}

void handleStartSweepProbe(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount != 6) return;
  int mask = args[0].toInt() & 0xFF;
  float levelPercent = args[1].toFloat();
  double f0 = (double)args[2].toFloat();
  double f1 = (double)args[3].toFloat();
  long chirpSamples = args[4].toInt();
  int nPasses = args[5].toInt();
  if (chirpSamples < 0) chirpSamples = 0;
  startSweepProbe(mask, levelPercent, f0, f1, (uint32_t)chirpSamples, nPasses);
}

// Track the chirp schedule from loop(): switch the soloed output at the
// midpoint of each inter-chirp gap. Timing here is deliberately
// non-critical; only the chirps themselves are sample-exact, and they live
// in ProbeSource. Works for either kind of session via the active
// preRoll/spacing/chirps captured at start time.
void probeLoop() {
  if (!probeActive) return;
  if (probeSource.isFinished()) {
    probeCleanup("PROBE DONE\n"); // swapped to "SWEEP DONE\n" when sweepMode
    return;
  }
  uint32_t s = probeSource.samplesElapsed();
  int slot = 0;
  if (s + activeSpacing / 2 >= activePreRoll) {
    slot = (int)((s + activeSpacing / 2 - activePreRoll) / activeSpacing);
  }
  if (slot >= activeChirps) slot = activeChirps - 1;
  if (slot != probeLastSlot) {
    probeLastSlot = slot;
    probeSolo = sweepMode ? sweepOrder[slot] : probeOrder[slot];
    Serial1.printf("%s CHIRP %d %d\n", sweepMode ? "SWEEP" : "PROBE", slot, probeSolo);
  }
}
