#include "PEQProcessor.h"
#include <math.h>

PEQProcessor::PEQProcessor() : AudioStream(1, inputQueue),
                               sampleRate(44100.0f), initialized(false), bypassed(false) {
  for (int i = 0; i < MAX_PEQ_BANDS; i++) {
    bands[i] = {1000.0f, 0.0f, 1.0f, false};
    svf[i] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false};
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

  bands[bandIndex].frequency = constrain(frequency, 20.0f, 20000.0f);
  bands[bandIndex].gain = constrain(gain, -15.0f, 15.0f);
  bands[bandIndex].q = constrain(q, 0.1f, 10.0f);
  bands[bandIndex].enabled = enabled;

  if (initialized) {
    updateFilter(bandIndex);
  }
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

  bands[bandIndex].enabled = enabled;

  if (initialized) {
    updateFilter(bandIndex);
  }
}

void PEQProcessor::clearAll() {
  for (int i = 0; i < MAX_PEQ_BANDS; i++) {
    bands[i].enabled = false;
    updateFilter(i);
  }
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

float PEQProcessor::calculateMaxEqBoost(const PEQBand* currentBands, int numBands) const {
  float maxBoost = 0.0f;
  // Sample the summed response logarithmically from 20Hz to 20kHz. Cascaded
  // filters multiply in linear gain, so their dB responses add.
  const int numSamples = 100;
  for (int i = 0; i < numSamples; ++i) {
    float freq = 20.0f * powf(1000.0f, (float)i / (numSamples - 1)); // 20Hz .. 20kHz
    float currentTotalGain = 0.0f;
    for (int j = 0; j < numBands; j++) {
      if (currentBands[j].enabled) {
        currentTotalGain += calculateBellFilter(freq, currentBands[j].frequency, currentBands[j].gain, currentBands[j].q);
      }
    }
    if (currentTotalGain > maxBoost) {
      maxBoost = currentTotalGain;
    }
  }
  return maxBoost;
}

void PEQProcessor::applyPreEQGain(float maxBoost, AudioAmplifier& leftAmp, AudioAmplifier& rightAmp) {
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
// Cytomic/Simper trapezoidal SVF, bell configuration - matches the RBJ
// peaking EQ response exactly (see "Solving the continuous SVF equations
// using trapezoidal integration", Andrew Simper).
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

  float freq = constrain(band.frequency, 20.0f, min(20000.0f, sampleRate * 0.49f));
  float q = constrain(band.q, 0.1f, 10.0f);
  float gain = constrain(band.gain, -15.0f, 15.0f);

  // Double precision for the coefficient math only; the audio path is float32
  double A = pow(10.0, (double)gain / 40.0);
  double g = tan(PI * (double)freq / (double)sampleRate);
  double k = 1.0 / ((double)q * A);
  double a1 = 1.0 / (1.0 + g * (g + k));

  if (!f.active) {
    // Band is (re)activating - start from silent integrators
    f.ic1eq = 0.0f;
    f.ic2eq = 0.0f;
  }
  f.a1 = (float)a1;
  f.a2 = (float)(g * a1);
  f.a3 = (float)(g * g * a1);
  f.m1 = (float)(k * (A * A - 1.0));
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

void PEQProcessor::animateToBands(const PEQBand* targetBands, int numBands, unsigned long durationMs) {
  if (!initialized) return;

  int maxBands = min(numBands, MAX_PEQ_BANDS);
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
    return;
  }

  animation.active = true;
  animation.startTime = millis();
  animation.duration = durationMs;
}

void PEQProcessor::setAnimationSpeed(unsigned long durationMs) {
  animation.duration = durationMs;
}

void PEQProcessor::updateAnimationState() {
  if (animation.active) {
    processAnimation();
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

  audio_block_t *output_block = allocate();
  if (!output_block) {
    return;
  }

  arm_float_to_q15(float_buffer, output_block->data, AUDIO_BLOCK_SAMPLES);

  transmit(output_block);
  release(output_block);
}

// Exact bell (peaking EQ) magnitude in dB, from the RBJ analog prototype:
//   H(s) = (s^2 + s*(A/Q) + 1) / (s^2 + s/(A*Q) + 1), evaluated at s = j*w/w0
// The SVF above is the bilinear (frequency-warped) realisation of the same
// prototype, so this matches the audible response.
float calculateBellFilter(float freq, float centerFreq, float gain, float q) {
  if (gain == 0.0f || q <= 0.0f || centerFreq <= 0.0f || freq <= 0.0f) return 0.0f;
  float A = powf(10.0f, gain / 40.0f);
  float O = freq / centerFreq;
  float c = 1.0f - O * O;
  c *= c;
  float nb = A * O / q;
  float db = O / (A * q);
  float num = c + nb * nb;
  float den = c + db * db;
  return 10.0f * log10f(num / den);
}
