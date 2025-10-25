#include "AudioFilterFIRFloat.h"
#include <Arduino.h>

// Default constructor implementation
AudioFilterFIRFloat::AudioFilterFIRFloat()
  : AudioStream(1, inputQueueArray),
    firCoeffs(nullptr),
    firState(nullptr),
    numTaps(0),
    enabled(true)
{
}

// Destructor implementation
AudioFilterFIRFloat::~AudioFilterFIRFloat() {
  delete[] firCoeffs; // Free the dynamically allocated coefficients buffer
  delete[] firState;  // Free the dynamically allocated state buffer
}

// setEnabled method implementation
void AudioFilterFIRFloat::setEnabled(bool enable) {
  __disable_irq();
  enabled = enable;
  __enable_irq();
}

// loadCoefficients method implementation
void AudioFilterFIRFloat::loadCoefficients(const float* coeffs, uint16_t newNumTaps) {
  // Step 1: Allocate new buffers with interrupts enabled.
  float* newCoeffs = nullptr;
  float* newState = nullptr;

  if (newNumTaps > 0 && coeffs != nullptr) {
    newCoeffs = new float[newNumTaps];
    if (!newCoeffs) {
      // Allocation failed, do nothing and return.
      return;
    }
    memcpy(newCoeffs, coeffs, sizeof(float) * newNumTaps);

    newState = new float[newNumTaps + AUDIO_BLOCK_SAMPLES - 1]();
    if (!newState) {
      delete[] newCoeffs; // Clean up partial allocation
      return;
    }
  }

  // Step 2: Atomically swap pointers and re-initialize the filter.
  float* oldCoeffs = nullptr;
  float* oldState = nullptr;

  __disable_irq();
  // --- Start Critical Section ---

  // Store old pointers for later deletion
  oldCoeffs = firCoeffs;
  oldState = firState;

  // Point to the new data
  firCoeffs = newCoeffs;
  firState = newState;
  numTaps = newNumTaps;

  // Re-initialize the CMSIS FIR instance with the new data
  if (numTaps > 0) {
    arm_fir_init_f32(&fir, numTaps, firCoeffs, firState, AUDIO_BLOCK_SAMPLES);
  }

  // --- End Critical Section ---
  __enable_irq();

  // Step 3: Free the old buffers with interrupts enabled.
  if (oldCoeffs) {
    delete[] oldCoeffs;
  }
  if (oldState) {
    delete[] oldState;
  }
}

// update() method implementation
void AudioFilterFIRFloat::update(void) {
  audio_block_t* block = receiveReadOnly(0);
  if (!block) return;

  // If filter is disabled or not configured, pass data through unchanged
  if (!enabled || numTaps == 0) {
    transmit(block);
    release(block);
    return;
  }

  audio_block_t* outBlock = allocate();
  if (!outBlock) {
    release(block);
    return;
  }

  float inputF32[AUDIO_BLOCK_SAMPLES];
  float outputF32[AUDIO_BLOCK_SAMPLES];

  arm_q15_to_float(block->data, inputF32, AUDIO_BLOCK_SAMPLES);
  release(block);

  __disable_irq();
  arm_fir_f32(&fir, inputF32, outputF32, AUDIO_BLOCK_SAMPLES);
  __enable_irq();

  arm_float_to_q15(outputF32, outBlock->data, AUDIO_BLOCK_SAMPLES);

  transmit(outBlock);
  release(outBlock);
}
