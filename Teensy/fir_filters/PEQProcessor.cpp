#include "PEQProcessor.h"
#include <math.h>

PEQProcessor::PEQProcessor() : AudioStream(1, inputQueue), 
                               sampleRate(44100.0f), initialized(false), bypassed(false) {
  for (int i = 0; i < MAX_PEQ_BANDS; i++) {
    bands[i] = {1000.0f, 0.0f, 1.0f, false};
  }
  
  animation.active = false;
  animation.startTime = 0;
  animation.duration = 1000;
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
    updateBiquadFilter(bandIndex);
  }
}

void PEQProcessor::setBand(int bandIndex, const PEQBand& band) {
  setBand(bandIndex, band.frequency, band.gain, band.q, band.enabled);
}

void PEQProcessor::updateBands(const PEQBand* bands, int numBands) {
  if (!initialized) return;
  
  int maxBands = min(numBands, MAX_PEQ_BANDS);
  
  for (int i = 0; i < maxBands; i++) {
    setBand(i, bands[i]);
  }
  
  for (int i = maxBands; i < MAX_PEQ_BANDS; i++) {
    enableBand(i, false);
  }
}

void PEQProcessor::enableBand(int bandIndex, bool enabled) {
  if (bandIndex < 0 || bandIndex >= MAX_PEQ_BANDS) return;
  
  bands[bandIndex].enabled = enabled;
  
  if (initialized) {
    updateBiquadFilter(bandIndex);
  }
}

void PEQProcessor::clearAll() {
  for (int i = 0; i < MAX_PEQ_BANDS; i++) {
    bands[i].enabled = false;
    if (initialized) {
      setUnityGain(i);
    }
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

void PEQProcessor::calculateCoefficients(const PEQBand& band, double coeffs[5]) {
  if (!band.enabled || band.gain == 0.0f) {
    coeffs[0] = 1.0; coeffs[1] = 0.0; coeffs[2] = 0.0; coeffs[3] = 0.0; coeffs[4] = 0.0;
    return;
  }
  
  float omega = 2.0 * PI * band.frequency / sampleRate;
  float cosOmega = cos(omega);
  float sinOmega = sin(omega);
  float A = pow(10.0, band.gain / 40.0);
  float alpha = sinOmega / (2.0 * band.q);
  
  float b0 = 1.0 + alpha * A;
  float b1 = -2.0 * cosOmega;
  float b2 = 1.0 - alpha * A;
  float a0 = 1.0 + alpha / A;
  float a1 = -2.0 * cosOmega;
  float a2 = 1.0 - alpha / A;
  
  // Normalize coefficients by a0
  coeffs[0] = b0 / a0;
  coeffs[1] = b1 / a0;
  coeffs[2] = b2 / a0;
  coeffs[3] = a1 / a0;
  coeffs[4] = a2 / a0;
}

void PEQProcessor::updateBiquadFilter(int bandIndex) {
  if (bandIndex < 0 || bandIndex >= MAX_PEQ_BANDS) return;
  
  double coeffs[5];
  calculateCoefficients(bands[bandIndex], coeffs);
  
  biquad_coeffs[bandIndex][0] = coeffs[0];
  biquad_coeffs[bandIndex][1] = coeffs[1];
  biquad_coeffs[bandIndex][2] = coeffs[2];
  biquad_coeffs[bandIndex][3] = -coeffs[3];
  biquad_coeffs[bandIndex][4] = -coeffs[4];

  

  arm_biquad_cascade_df1_init_f32(&biquad_insts[bandIndex], 1, biquad_coeffs[bandIndex], biquad_states[bandIndex]);
}

void PEQProcessor::setUnityGain(int bandIndex) {
  if (bandIndex < 0 || bandIndex >= MAX_PEQ_BANDS) return;
  
  biquad_coeffs[bandIndex][0] = 1.0f;
  biquad_coeffs[bandIndex][1] = 0.0f;
  biquad_coeffs[bandIndex][2] = 0.0f;
  biquad_coeffs[bandIndex][3] = 0.0f;
  biquad_coeffs[bandIndex][4] = 0.0f;

  arm_biquad_cascade_df1_init_f32(&biquad_insts[bandIndex], 1, biquad_coeffs[bandIndex], biquad_states[bandIndex]);
}

void PEQProcessor::animateToBands(const PEQBand* targetBands, int numBands, unsigned long durationMs) {
  if (!initialized) return;
  
  for (int i = 0; i < MAX_PEQ_BANDS; i++) {
    animation.startBands[i] = bands[i];
  }
  
  int maxBands = min(numBands, MAX_PEQ_BANDS);
  for (int i = 0; i < maxBands; i++) {
    animation.targetBands[i] = targetBands[i];
  }
  
  for (int i = maxBands; i < MAX_PEQ_BANDS; i++) {
    animation.targetBands[i] = {1000.0f, 0.0f, 1.0f, false};
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
      bands[i] = animation.targetBands[i];
      updateBiquadFilter(i);
    }
    animation.active = false;
    return;
  }
  
  float progress = (float)elapsed / (float)animation.duration;
  progress = progress * progress * (3.0f - 2.0f * progress);
  
  for (int i = 0; i < MAX_PEQ_BANDS; i++) {
    bands[i].frequency = interpolate(animation.startBands[i].frequency, 
                                   animation.targetBands[i].frequency, progress);
    bands[i].gain = interpolate(animation.startBands[i].gain, 
                              animation.targetBands[i].gain, progress);
    bands[i].q = interpolate(animation.startBands[i].q, 
                           animation.targetBands[i].q, progress);
    
    bands[i].enabled = (progress < 0.5f) ? animation.startBands[i].enabled : 
                                          animation.targetBands[i].enabled;
    
    updateBiquadFilter(i);
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

  int active_bands = 0;
  for (int i = 0; i < MAX_PEQ_BANDS; i++) {
    if (bands[i].enabled) {
      arm_biquad_cascade_df1_f32(&biquad_insts[i], float_buffer, float_buffer, AUDIO_BLOCK_SAMPLES);
      active_bands++;
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