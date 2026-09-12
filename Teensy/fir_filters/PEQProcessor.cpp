#include "PEQProcessor.h"
#include "DynamicEqMath.h" // shelf corner frequencies
#include <math.h>

PEQProcessor::PEQProcessor() : AudioStream(1, inputQueue),
                               sampleRate(44100.0f), initialized(false), bypassed(false),
                               appliedPreEqBoostDb(NAN),
                               shelfGainDb(0.0f), shelfStartGainDb(0.0f),
                               shelfTargetGainDb(0.0f), shelfStartTime(0),
                               shelfDuration(50), shelfMoving(false) {
  for (int i = 0; i < MAX_PEQ_BANDS; i++) {
    bands[i] = {1000.0f, 0.0f, 1.0f, false};
    svf[i] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false};
  }
  for (int i = 0; i < LOUDNESS_SHELF_COUNT; i++) {
    shelves[i] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false};
  }

  animation.active = false;
  animation.startTime = 0;
  animation.duration = 50;
}

void PEQProcessor::begin(float sampleRate) {
  this->sampleRate = sampleRate;
  initialized = true;
  clearAll();
}

void PEQProcessor::setBand(int bandIndex, float frequency, float gain, float q, bool enabled) {
  if (bandIndex < 0 || bandIndex >= MAX_PEQ_BANDS) return;

  // Called from serial (loop) context; update() reads this state from the
  // audio interrupt.
  AudioNoInterrupts();

  bands[bandIndex].frequency = constrain(frequency, 20.0f, 20000.0f);
  bands[bandIndex].gain = constrain(gain, -15.0f, 15.0f);
  bands[bandIndex].q = constrain(q, 0.1f, 10.0f);
  bands[bandIndex].enabled = enabled;

  if (initialized) {
    updateFilter(bandIndex);
  }

  AudioInterrupts();
}

void PEQProcessor::setBand(int bandIndex, const PEQBand& band) {
  setBand(bandIndex, band.frequency, band.gain, band.q, band.enabled);
}

void PEQProcessor::updateBands(const PEQBand* newBands, int numBands) {
  if (!initialized) return;

  int maxBands = min(numBands, MAX_PEQ_BANDS);

  for (int i = 0; i < maxBands; i++) {
    setBand(i, newBands[i]);
  }

  for (int i = maxBands; i < MAX_PEQ_BANDS; i++) {
    enableBand(i, false);
  }
}

void PEQProcessor::enableBand(int bandIndex, bool enabled) {
  if (bandIndex < 0 || bandIndex >= MAX_PEQ_BANDS) return;

  // Called from serial (loop) context; update() reads this state from the
  // audio interrupt.
  AudioNoInterrupts();

  bands[bandIndex].enabled = enabled;

  if (initialized) {
    updateFilter(bandIndex);
  }

  AudioInterrupts();
}

void PEQProcessor::clearAll() {
  // Called from serial (loop) context; update() reads this state from the
  // audio interrupt.
  AudioNoInterrupts();

  for (int i = 0; i < MAX_PEQ_BANDS; i++) {
    bands[i].enabled = false;
    updateFilter(i);
  }

  AudioInterrupts();
}

PEQBand PEQProcessor::getBand(int bandIndex) const {
  if (bandIndex < 0 || bandIndex >= MAX_PEQ_BANDS) {
    return {1000.0f, 0.0f, 1.0f, false};
  }
  return bands[bandIndex];
}

int PEQProcessor::getActiveBandCount() const {
  int count = 0;
  for (int i = 0; i < MAX_PEQ_BANDS; i++) {
    if (bands[i].enabled) count++;
  }
  return count;
}

void PEQProcessor::applyPreEQGain(float maxBoost, AudioAmplifier& leftAmp, AudioAmplifier& rightAmp) {
  // The dynamic EQ re-derives the pad on every volume tick, and this writes
  // the amp gain straight in (no ramp) and logs a line. Both are wasted -
  // and the log is noise in the middle of a slider drag - when the pad has
  // not actually moved. NAN != NAN, so the first call always writes.
  if (maxBoost == appliedPreEqBoostDb) return;
  appliedPreEqBoostDb = maxBoost;

  float linearGain = 1.0f; // Default to 1.0 (0dB) if no boost or only cuts
  if (maxBoost > 0.0f) {
    // Convert dB to linear gain and apply as attenuation
    linearGain = 1.0f / powf(10.0f, maxBoost / 20.0f);
  }
  // Ensure a reasonable minimum gain to prevent silence or extreme attenuation
  if (linearGain < 0.01f) linearGain = 0.01f;

  leftAmp.gain(linearGain);
  rightAmp.gain(linearGain);
  Serial.println("Pre-EQ gain set to: " + String(linearGain) + " (max boost: " + String(maxBoost) + "dB)");
}

// Recompute the SVF coefficients for one band from bands[bandIndex].
// The coefficient math itself lives in PEQMath.cpp so it can be verified
// host-side against the RBJ peaking-EQ reference.
void PEQProcessor::updateFilter(int bandIndex) {
  if (bandIndex < 0 || bandIndex >= MAX_PEQ_BANDS) return;

  const PEQBand& band = bands[bandIndex];
  SVFBand& f = svf[bandIndex];

  bool active = band.enabled && band.gain != 0.0f;
  if (!active) {
    if (f.active) {
      // Reset states so a later re-enable doesn't replay stale energy
      f.ic1eq = 0.0f;
      f.ic2eq = 0.0f;
    }
    f.active = false;
    return;
  }

  PeqSvfCoeffs c = peqComputeBellSvf(band.frequency, band.gain, band.q, sampleRate);

  if (!f.active) {
    // Band is (re)activating - start from silent integrators
    f.ic1eq = 0.0f;
    f.ic2eq = 0.0f;
  }
  f.a1 = c.a1;
  f.a2 = c.a2;
  f.a3 = c.a3;
  f.m1 = c.m1;
  f.active = true;
}

void PEQProcessor::processBand(int bandIndex, float32_t* buffer, int numSamples) {
  SVFBand& f = svf[bandIndex];
  float a1 = f.a1, a2 = f.a2, a3 = f.a3, m1 = f.m1;
  float ic1 = f.ic1eq, ic2 = f.ic2eq;

  for (int i = 0; i < numSamples; i++) {
    float v0 = buffer[i];
    float v3 = v0 - ic2;
    float v1 = a1 * ic1 + a2 * v3;
    float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    buffer[i] = v0 + m1 * v1; // bell: input plus scaled bandpass
  }

  f.ic1eq = ic1;
  f.ic2eq = ic2;
}

// --- Loudness compensation shelves ---

// Recompute both shelf stages for shelfGainDb. At 0dB they drop out of the
// cascade entirely, so a preference curve with no compensation running costs
// nothing per block.
void PEQProcessor::updateShelves() {
  static const float shelfFreq[LOUDNESS_SHELF_COUNT] = {
      LOUDNESS_SHELF1_HZ, LOUDNESS_SHELF2_HZ};

  bool active = shelfGainDb != 0.0f;
  for (int i = 0; i < LOUDNESS_SHELF_COUNT; i++) {
    SVFShelf& s = shelves[i];

    if (!active) {
      if (s.active) {
        // Reset states so a later re-enable doesn't replay stale energy
        s.ic1eq = 0.0f;
        s.ic2eq = 0.0f;
      }
      s.active = false;
      continue;
    }

    PeqShelfSvfCoeffs c = peqComputeHighShelfSvf(shelfFreq[i], shelfGainDb, sampleRate);

    if (!s.active) {
      s.ic1eq = 0.0f;
      s.ic2eq = 0.0f;
    }
    s.a1 = c.a1;
    s.a2 = c.a2;
    s.a3 = c.a3;
    s.m0 = c.m0;
    s.m1 = c.m1;
    s.m2 = c.m2;
    s.active = true;
  }
}

void PEQProcessor::setLoudnessShelves(float gainDb, unsigned long durationMs) {
  if (!initialized) return;

  // Serial (loop) context against the audio interrupt, same as the bands.
  AudioNoInterrupts();

  // Already there, or already on the way there - don't restart the morph
  if (gainDb == shelfTargetGainDb) {
    AudioInterrupts();
    return;
  }

  if (durationMs == 0) {
    shelfMoving = false;
    shelfGainDb = gainDb;
    shelfTargetGainDb = gainDb;
    updateShelves();
    AudioInterrupts();
    return;
  }

  shelfStartGainDb = shelfGainDb;
  shelfTargetGainDb = gainDb;
  shelfStartTime = millis();
  shelfDuration = durationMs;
  shelfMoving = true;

  AudioInterrupts();
}

// Same smoothstep the band morph uses, on its own clock: the shelves and the
// bands are set by separate calls and must not reset each other's timer.
void PEQProcessor::processShelfMorph() {
  unsigned long elapsed = millis() - shelfStartTime;

  if (elapsed >= shelfDuration) {
    shelfGainDb = shelfTargetGainDb;
    shelfMoving = false;
    updateShelves();
    return;
  }

  float progress = (float)elapsed / (float)shelfDuration;
  progress = progress * progress * (3.0f - 2.0f * progress); // smoothstep
  shelfGainDb = interpolate(shelfStartGainDb, shelfTargetGainDb, progress);
  updateShelves();
}

void PEQProcessor::processShelf(int shelfIndex, float32_t* buffer, int numSamples) {
  SVFShelf& s = shelves[shelfIndex];
  float a1 = s.a1, a2 = s.a2, a3 = s.a3;
  float m0 = s.m0, m1 = s.m1, m2 = s.m2;
  float ic1 = s.ic1eq, ic2 = s.ic2eq;

  for (int i = 0; i < numSamples; i++) {
    float v0 = buffer[i];
    float v3 = v0 - ic2;
    float v1 = a1 * ic1 + a2 * v3;
    float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    buffer[i] = m0 * v0 + m1 * v1 + m2 * v2; // shelf mixes all three outputs
  }

  s.ic1eq = ic1;
  s.ic2eq = ic2;
}

void PEQProcessor::animateToBands(const PEQBand* targetBands, int numBands, unsigned long durationMs) {
  if (!initialized) return;

  // This runs in serial (loop) context while update() reads and writes the
  // same animation state from the audio interrupt - keep the two apart.
  AudioNoInterrupts();

  int maxBands = min(numBands, MAX_PEQ_BANDS);
  bool anyMoving = false;
  for (int i = 0; i < MAX_PEQ_BANDS; i++) {
    animation.startBands[i] = bands[i];
    if (i < maxBands) {
      animation.targetBands[i] = targetBands[i];
    } else {
      animation.targetBands[i] = {1000.0f, 0.0f, 1.0f, false};
    }
    // Only animate bands that are actually changing
    animation.bandMoving[i] =
        animation.startBands[i].frequency != animation.targetBands[i].frequency ||
        animation.startBands[i].gain != animation.targetBands[i].gain ||
        animation.startBands[i].q != animation.targetBands[i].q ||
        animation.startBands[i].enabled != animation.targetBands[i].enabled;
    anyMoving = anyMoving || animation.bandMoving[i];
  }

  // The dynamic EQ re-pushes the whole curve on every volume tick, and below
  // the reference level none of it moves. Don't run an animation that would
  // recompute nothing for 50ms of audio interrupts.
  if (!anyMoving) {
    animation.active = false;
    AudioInterrupts();
    return;
  }

  if (durationMs == 0) {
    // Apply immediately
    for (int i = 0; i < MAX_PEQ_BANDS; i++) {
      if (animation.bandMoving[i]) {
        bands[i] = animation.targetBands[i];
        updateFilter(i);
      }
    }
    animation.active = false;
    AudioInterrupts();
    return;
  }

  animation.active = true;
  animation.startTime = millis();
  animation.duration = durationMs;

  AudioInterrupts();
}

void PEQProcessor::setAnimationSpeed(unsigned long durationMs) {
  animation.duration = durationMs;
}

void PEQProcessor::updateAnimationState() {
  if (animation.active) {
    processAnimation();
  }
  if (shelfMoving) {
    processShelfMorph();
  }
}

bool PEQProcessor::isAnimating() const {
  return animation.active;
}

void PEQProcessor::stopAnimation() {
  animation.active = false;
}

void PEQProcessor::processAnimation() {
  unsigned long currentTime = millis();
  unsigned long elapsed = currentTime - animation.startTime;

  if (elapsed >= animation.duration) {
    for (int i = 0; i < MAX_PEQ_BANDS; i++) {
      if (animation.bandMoving[i]) {
        bands[i] = animation.targetBands[i];
        updateFilter(i);
      }
    }
    animation.active = false;
    return;
  }

  float progress = (float)elapsed / (float)animation.duration;
  progress = progress * progress * (3.0f - 2.0f * progress); // smoothstep

  for (int i = 0; i < MAX_PEQ_BANDS; i++) {
    if (!animation.bandMoving[i]) continue;

    bands[i].frequency = interpolate(animation.startBands[i].frequency,
                                   animation.targetBands[i].frequency, progress);
    bands[i].gain = interpolate(animation.startBands[i].gain,
                              animation.targetBands[i].gain, progress);
    bands[i].q = interpolate(animation.startBands[i].q,
                           animation.targetBands[i].q, progress);

    bands[i].enabled = (progress < 0.5f) ? animation.startBands[i].enabled :
                                          animation.targetBands[i].enabled;

    updateFilter(i);
  }
}

float PEQProcessor::interpolate(float start, float end, float progress) {
  return start + (end - start) * progress;
}

void PEQProcessor::setBypass(bool bypassed) {
  this->bypassed = bypassed;
}

bool PEQProcessor::isBypassed() const {
  return bypassed;
}

void PEQProcessor::toggleBypass() {
  setBypass(!bypassed);
}

void PEQProcessor::update(void) {
  updateAnimationState();

  audio_block_t *block = receiveReadOnly();
  if (!block) return;

  if (bypassed) {
    transmit(block);
    release(block);
    return;
  }

  float32_t float_buffer[AUDIO_BLOCK_SAMPLES];
  arm_q15_to_float(block->data, float_buffer, AUDIO_BLOCK_SAMPLES);
  release(block);

  // Cascade all active bands
  for (int i = 0; i < MAX_PEQ_BANDS; i++) {
    if (svf[i].active) {
      processBand(i, float_buffer, AUDIO_BLOCK_SAMPLES);
    }
  }

  // Then the loudness shelves: part of the same preference curve, so they
  // live behind the same bypass, but they are not user bands.
  for (int i = 0; i < LOUDNESS_SHELF_COUNT; i++) {
    if (shelves[i].active) {
      processShelf(i, float_buffer, AUDIO_BLOCK_SAMPLES);
    }
  }

  audio_block_t *output_block = allocate();
  if (!output_block) {
    return;
  }

  arm_float_to_q15(float_buffer, output_block->data, AUDIO_BLOCK_SAMPLES);

  transmit(output_block);
  release(output_block);
}

// calculateBellFilter (the exact bell magnitude response used for gain
// compensation) lives in PEQMath.cpp alongside the coefficient math.
