#include "AudioFilterFIRFloat.h"

// Default constructor implementation
AudioFilterFIRFloat::AudioFilterFIRFloat()
  : AudioStream(1, inputQueueArray),
    firCoeffs(nullptr),
    firState(nullptr),
    numTaps(0)
{
}

// Destructor implementation
AudioFilterFIRFloat::~AudioFilterFIRFloat() {
  delete[] firCoeffs; // Free the dynamically allocated coefficients buffer
  delete[] firState;  // Free the dynamically allocated state buffer
}

// loadCoefficients method implementation
void AudioFilterFIRFloat::loadCoefficients(const float* coeffs, uint16_t newNumTaps) {
  // Free existing buffers
  delete[] firCoeffs;
  delete[] firState;
  firCoeffs = nullptr;
  firState = nullptr;

  // Update taps count
  numTaps = newNumTaps;

  if (numTaps > 0 && coeffs != nullptr) {
    // Allocate memory for coefficients and copy them
    firCoeffs = new float[numTaps];
    if (firCoeffs) {
      memcpy(firCoeffs, coeffs, sizeof(float) * numTaps);
    }

    // Allocate state buffer
    firState = new float[numTaps + AUDIO_BLOCK_SAMPLES - 1](); // Value-initialized to zero

    // If either allocation failed, reset to a safe empty state
    if (!firCoeffs || !firState) {
      delete[] firCoeffs;
      delete[] firState;
      firCoeffs = nullptr;
      firState = nullptr;
      numTaps = 0;
    }
  }

  // Re-initialize the CMSIS FIR instance only if the filter is active
  if (numTaps > 0) {
    arm_fir_init_f32(&fir, numTaps, firCoeffs, firState, AUDIO_BLOCK_SAMPLES);
  }
}

// update() method implementation
void AudioFilterFIRFloat::update(void) {
  audio_block_t* block = receiveReadOnly(0);
  if (!block) return;

  // If filter is not configured, pass data through unchanged
  if (numTaps == 0) {
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
  release(block); // Release input block as soon as we are done with it

  arm_fir_f32(&fir, inputF32, outputF32, AUDIO_BLOCK_SAMPLES);

  arm_float_to_q15(outputF32, outBlock->data, AUDIO_BLOCK_SAMPLES);

  transmit(outBlock);
  release(outBlock);
}