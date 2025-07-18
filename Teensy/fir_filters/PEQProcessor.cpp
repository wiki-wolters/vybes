#include "PEQProcessor.h"
#include <math.h>

PEQProcessor::PEQProcessor() : AudioStream(1, inputQueue), 
                               sampleRate(44100.0f), initialized(false), bypassed(false) {
  // PEQProcessor_instance_count++; // This is in the .h if constructor is inlined there
  // Initialize all bands to disabled/unity gain
  for (int i = 0; i < MAX_PEQ_BANDS; i++) {
    bands[i] = {1000.0f, 0.0f, 1.0f, false};
    // connections[i] = nullptr; // Removed
  }
  // connections[MAX_PEQ_BANDS] = nullptr; // Removed
  
  // Initialize animation state
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
  
  // Update provided bands
  for (int i = 0; i < maxBands; i++) {
    setBand(i, bands[i]);
  }
  
  // Disable remaining bands
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



AudioFilterBiquad& PEQProcessor::getBiquadFilter(int index) {
  if (index < 0 || index >= MAX_PEQ_BANDS) {
    return biquadFilters[0];  // Return first filter as fallback
  }
  return biquadFilters[index];
}

void PEQProcessor::calculateCoefficients(const PEQBand& band, double coeffs[5]) {
  if (!band.enabled || band.gain == 0.0f) {
    // Unity gain coefficients
    coeffs[0] = 1.0;  // b0
    coeffs[1] = 0.0;  // b1
    coeffs[2] = 0.0;  // b2
    coeffs[3] = 0.0;  // a1
    coeffs[4] = 0.0;  // a2
    return;
  }
  
  // Calculate peaking EQ coefficients
  float omega = 2.0 * PI * band.frequency / sampleRate;
  float cosOmega = cos(omega);
  float sinOmega = sin(omega);
  
  // Convert gain from dB to linear
  float A = pow(10.0, band.gain / 40.0);  // sqrt of power gain
  
  // Calculate alpha
  float alpha = sinOmega / (2.0 * band.q);
  
  // Calculate coefficients
  float b0 = 1.0 + alpha * A;
  float b1 = -2.0 * cosOmega;
  float b2 = 1.0 - alpha * A;
  float a0 = 1.0 + alpha / A;
  float a1 = -2.0 * cosOmega;
  float a2 = 1.0 - alpha / A;
  
  // Normalize by a0 and store in array
  coeffs[0] = b0 / a0;  // b0
  coeffs[1] = b1 / a0;  // b1
  coeffs[2] = b2 / a0;  // b2
  coeffs[3] = a1 / a0;  // a1
  coeffs[4] = a2 / a0;  // a2
}

void PEQProcessor::updateBiquadFilter(int bandIndex) {
  if (bandIndex < 0 || bandIndex >= MAX_PEQ_BANDS) return;
  
  double coeffs[5];
  calculateCoefficients(bands[bandIndex], coeffs);
  biquadFilters[bandIndex].setCoefficients(0, coeffs);
}

void PEQProcessor::setUnityGain(int bandIndex) {
  if (bandIndex < 0 || bandIndex >= MAX_PEQ_BANDS) return;
  
  double unityCoeffs[5] = {1.0, 0.0, 0.0, 0.0, 0.0};
  biquadFilters[bandIndex].setCoefficients(0, unityCoeffs);
}

// Animation methods
void PEQProcessor::animateToBands(const PEQBand* targetBands, int numBands, unsigned long durationMs) {
  if (!initialized) return;
  
  // Store current state as starting point
  for (int i = 0; i < MAX_PEQ_BANDS; i++) {
    animation.startBands[i] = bands[i];
  }
  
  // Store target state
  int maxBands = min(numBands, MAX_PEQ_BANDS);
  for (int i = 0; i < maxBands; i++) {
    animation.targetBands[i] = targetBands[i];
  }
  
  // Disable remaining bands in target
  for (int i = maxBands; i < MAX_PEQ_BANDS; i++) {
    animation.targetBands[i] = {1000.0f, 0.0f, 1.0f, false};
  }
  
  // Start animation
  animation.active = true;
  animation.startTime = millis();
  animation.duration = durationMs;
}

void PEQProcessor::setAnimationSpeed(unsigned long durationMs) {
  animation.duration = durationMs;
}

void PEQProcessor::updateAnimationState() {
  if (animation.active) {
    processAnimation(); // Corrected call
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
    // Animation complete - set final values
    for (int i = 0; i < MAX_PEQ_BANDS; i++) {
      bands[i] = animation.targetBands[i];
      updateBiquadFilter(i);
    }
    animation.active = false;
    return;
  }
  
  // Calculate progress (0.0 to 1.0)
  float progress = (float)elapsed / (float)animation.duration;
  
  // Apply easing curve (ease-in-out)
  progress = progress * progress * (3.0f - 2.0f * progress);
  
  // Interpolate between start and target values
  for (int i = 0; i < MAX_PEQ_BANDS; i++) {
    bands[i].frequency = interpolate(animation.startBands[i].frequency, 
                                   animation.targetBands[i].frequency, progress);
    bands[i].gain = interpolate(animation.startBands[i].gain, 
                              animation.targetBands[i].gain, progress);
    bands[i].q = interpolate(animation.startBands[i].q, 
                           animation.targetBands[i].q, progress);
    
    // Handle enabled state (switch at 50% progress)
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

// Required by AudioStream
void PEQProcessor::update(void) {
  updateAnimationState(); // Update animation parameters if any

  audio_block_t *block = receiveReadOnly(); // Reads from inputQueueArray[0]
  if (!block) return;

  if (bypassed) {
    transmit(block); // Transmits to connected output
    release(block);
    return;
  }

  // TODO: Implement actual PEQ processing here.
  // For now, this is a pass-through. The input block is transmitted as is.
  // The block->data needs to be processed by the enabled biquadFilters.
  // This involves iterating through biquadFilters that are enabled
  // and applying their filtering effect to the samples in block->data.
  // A common approach is to process in-place if possible, or use an
  // intermediate buffer if necessary for a chain of filters.
  // Example for a single biquad (not a chain, and needs adaptation):
  // if (bands[0].enabled) {
  //   biquadFilters[0].process(block->data, block->data, AUDIO_BLOCK_SAMPLES);
  // }
  // For a chain, you'd typically pass output of one to input of next.

  transmit(block); // Transmit the (currently unprocessed or partially processed) block
  release(block);
}
