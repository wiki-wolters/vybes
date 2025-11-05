#include "PEQProcessor.h"
#include <math.h>

PEQProcessor::PEQProcessor() : AudioStream(1, inputQueue), 
                               sampleRate(44100.0f), initialized(false), bypassed(false) {
  for (int i = 0; i < MAX_PEQ_BANDS; i++) {
    bands[i] = {1000.0f, 0.0f, 1.0f, false};
    // Initialize Chamberlin state variables
    chamberlin_states[i] = {0.0f, 0.0f, 0.0f, 0.0f};
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
    updateFilter(bandIndex);
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
    updateFilter(bandIndex);
  }
}

void PEQProcessor::clearAll() {
  for (int i = 0; i < MAX_PEQ_BANDS; i++) {
    bands[i].enabled = false;
    // Clear both filter types
    chamberlin_states[i] = {0.0f, 0.0f, 0.0f, 0.0f};
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

float PEQProcessor::calculateMaxEqBoost(const PEQBand* currentBands, int numBands) const {
  float maxBoost = 0.0f;
  // Iterate over a range of frequencies to find the peak gain
  float sampleFrequencies[] = {20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f};
  for (float freq : sampleFrequencies) {
    float currentTotalGain = 0.0f;
    for (int i = 0; i < numBands; i++) {
      if (currentBands[i].enabled) {
        currentTotalGain += calculateBellFilter(freq, currentBands[i].frequency, currentBands[i].gain, currentBands[i].q);
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
    linearGain = 1.0f / pow(10.0f, maxBoost / 20.0f);
  }
  // Ensure a reasonable minimum gain to prevent silence or extreme attenuation
  if (linearGain < 0.01f) linearGain = 0.01f; // Example minimum, adjust as needed

  leftAmp.gain(linearGain);
  rightAmp.gain(linearGain);
  Serial.println("Pre-EQ gain set to: " + String(linearGain) + " (max boost: " + String(maxBoost) + "dB)");
}

FilterType PEQProcessor::getOptimalFilterType(float frequency) {
  return (frequency < 1000.0f) ? FILTER_CHAMBERLIN : FILTER_BIQUAD;
}

void PEQProcessor::updateFilter(int bandIndex) {
  if (bandIndex < 0 || bandIndex >= MAX_PEQ_BANDS) return;
  
  FilterType filterType = getOptimalFilterType(bands[bandIndex].frequency);
  filter_types[bandIndex] = filterType;
  
  switch (filterType) {
    case FILTER_CHAMBERLIN:
      updateChamberlinFilter(bandIndex);
      break;
    case FILTER_BIQUAD:
      updateBiquadFilter(bandIndex);
      break;
  }
}

void PEQProcessor::updateChamberlinFilter(int bandIndex) {
  if (bandIndex < 0 || bandIndex >= MAX_PEQ_BANDS) return;
  
  const PEQBand& band = bands[bandIndex];
  ChamberlinState& state = chamberlin_states[bandIndex];
  
  if (!band.enabled || band.gain == 0.0f) {
    // Set to bypass state
    state.f = 0.0f;
    state.q_inv = 0.0f;
    state.low = 0.0f;
    state.band = 0.0f;
    return;
  }
  
  // Calculate Chamberlin parameters
  double omega = 2.0 * PI * (double)band.frequency / (double)sampleRate;
  state.f = 2.0f * sin(omega * 0.5f);  // More stable than direct sin(omega)
  state.q_inv = 1.0f / band.q;
  
  // Constrain f to prevent instability
  if (state.f > 1.99f) state.f = 1.99f;
}

void PEQProcessor::calculateCoefficients(const PEQBand& band, double coeffs[5]) {
  if (!band.enabled || band.gain == 0.0f) {
    coeffs[0] = 1.0; coeffs[1] = 0.0; coeffs[2] = 0.0; coeffs[3] = 0.0; coeffs[4] = 0.0;
    return;
  }
  
  double omega = 2.0 * PI * (double)band.frequency / (double)sampleRate;
  double cosOmega = cos(omega);
  double sinOmega = sin(omega);
  double A = pow(10.0, (double)band.gain / 40.0);
  double alpha = sinOmega / (2.0 * (double)band.q);
  
  double b0 = 1.0 + alpha * A;
  double b1 = -2.0 * cosOmega;
  double b2 = 1.0 - alpha * A;
  double a0 = 1.0 + alpha / A;
  double a1 = -2.0 * cosOmega;
  double a2 = 1.0 - alpha / A;
  
  // Normalize coefficients by a0
  coeffs[0] = b0 / a0;
  coeffs[1] = b1 / a0;
  coeffs[2] = b2 / a0;
  coeffs[3] = a1 / a0;
  coeffs[4] = a2 / a0;
}

bool PEQProcessor::validateCoefficients(double coeffs[5]) {
  // Check for NaN or infinity
  for (int i = 0; i < 5; i++) {
    if (!isfinite(coeffs[i])) return false;
  }
  
  // Check stability (poles inside unit circle)
  double a1 = coeffs[3], a2 = coeffs[4];
  return (abs(a2) < 1.0 && abs(a1) < (1.0 + abs(a2)));
}

void PEQProcessor::updateBiquadFilter(int bandIndex) {
  if (bandIndex < 0 || bandIndex >= MAX_PEQ_BANDS) return;
  
  double coeffs[5];
  calculateCoefficients(bands[bandIndex], coeffs);
  
  // Validate coefficients before using them
  if (!validateCoefficients(coeffs)) {
    setUnityGain(bandIndex);
    return;
  }
  
  biquad_coeffs[bandIndex][0] = coeffs[0];
  biquad_coeffs[bandIndex][1] = coeffs[1];
  biquad_coeffs[bandIndex][2] = coeffs[2];
  biquad_coeffs[bandIndex][3] = -coeffs[3];
  biquad_coeffs[bandIndex][4] = -coeffs[4];

  arm_biquad_cascade_df1_init_f32(&biquad_insts[bandIndex], 1, biquad_coeffs[bandIndex], biquad_states[bandIndex]);
}

void PEQProcessor::setUnityGain(int bandIndex) {
  if (bandIndex < 0 || bandIndex >= MAX_PEQ_BANDS) return;
  
  // Set biquad to unity
  biquad_coeffs[bandIndex][0] = 1.0f;
  biquad_coeffs[bandIndex][1] = 0.0f;
  biquad_coeffs[bandIndex][2] = 0.0f;
  biquad_coeffs[bandIndex][3] = 0.0f;
  biquad_coeffs[bandIndex][4] = 0.0f;
  arm_biquad_cascade_df1_init_f32(&biquad_insts[bandIndex], 1, biquad_coeffs[bandIndex], biquad_states[bandIndex]);
  
  // Set Chamberlin to unity
  chamberlin_states[bandIndex] = {0.0f, 0.0f, 0.0f, 0.0f};
}

void PEQProcessor::processChamberlinBand(int bandIndex, float32_t* buffer, int numSamples) {
  ChamberlinState& state = chamberlin_states[bandIndex];
  const PEQBand& band = bands[bandIndex];
  
  if (!band.enabled || band.gain == 0.0f || state.f == 0.0f) {
    return; // No processing needed
  }
  
  float gain_linear = pow(10.0f, band.gain / 20.0f) - 1.0f; // Convert to additive gain
  
  for (int i = 0; i < numSamples; i++) {
    float input = buffer[i];
    
    // Chamberlin state variable filter equations
    state.low += state.f * state.band;
    float high = input - state.low - state.q_inv * state.band;
    state.band += state.f * high;
    
    // For PEQ, we want to boost/cut the bandpass component
    buffer[i] = input + (state.band * gain_linear);
  }
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
      updateFilter(i);
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

  // Process each enabled band with appropriate filter type
  for (int i = 0; i < MAX_PEQ_BANDS; i++) {
    if (bands[i].enabled) {
      switch (filter_types[i]) {
        case FILTER_CHAMBERLIN:
          processChamberlinBand(i, float_buffer, AUDIO_BLOCK_SAMPLES);
          break;
        case FILTER_BIQUAD:
          arm_biquad_cascade_df1_f32(&biquad_insts[i], float_buffer, float_buffer, AUDIO_BLOCK_SAMPLES);
          break;
      }
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

// Bell filter calculation (peaking EQ) - adapted from WebUI
float calculateBellFilter(float freq, float centerFreq, float gain, float q) {
  if (q == 0) return 0; // Avoid division by zero
  float ratio = freq / centerFreq;
  float bandwidth = 1 / q;
  // This formula is a simplification and might not perfectly match all digital biquad implementations
  // but it provides a good approximation for calculating the curve for pre-gain compensation.
  float response = gain / (1 + pow((ratio - 1/ratio) / bandwidth, 2));
  return response;
}