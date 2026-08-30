#include "DelayProbe.h"

#include <Arduino.h>
#include <Audio.h> // AudioNoInterrupts/AudioInterrupts

#include "SketchState.h"
#include "teensy_protocol.h" // PROBE_* chirp/schedule contract

// probeLoop() switches which output is soloed between chirps, and the
// sketch's outputTargetGain() consults probeGainForOutput(). The solo rides
// the existing amp ramp, so switching is click-free. probeGain is applied
// instead of the normal gain/mute/volume product so a muted device or zero
// volume can't silence the measurement.
static bool   probeActive = false;
static int    probeSolo = -1;               // output the current chirp leaves through
static float  probeGain = 0.0f;             // amp gain for the soloed output
static int8_t probeOrder[2 * NUM_OUTPUTS];  // masked outputs ascending, then reversed
static int    probeChirps = 0;
static int    probeLastSlot = -1;

bool probeIsActive() {
  return probeActive;
}

float probeGainForOutput(int ch) {
  return (ch == probeSolo) ? probeGain : 0.0f;
}

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

  // The probe needs silence between chirps; SD playback rides the aux
  // mixer's input 2, which the probe's input muting leaves open.
  if (sdPlayer.isActive()) {
    sdPlayer.stop();
    recStateDirty = true;
  }

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
