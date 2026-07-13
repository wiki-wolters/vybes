#include "AudioFilterFIRFloat.h"
#include <Arduino.h>
#include <arm_const_structs.h>
#include <arm_common_tables.h>

// The hand-built rfft instance below is only valid for a 256-point FFT
static_assert(AUDIO_BLOCK_SAMPLES == 128, "fast convolution engine assumes 128-sample audio blocks");

// Default constructor implementation
AudioFilterFIRFloat::AudioFilterFIRFloat()
  : AudioStream(1, inputQueueArray),
    firCoeffs(nullptr),
    firState(nullptr),
    partSpectra(nullptr),
    fdl(nullptr),
    numPartitions(0),
    fdlIndex(0),
    numTaps(0),
    useFastConvolution(false),
    loadedFast(false),
    enabled(true),
    wasProcessing(false)
{
  // Build the FFT instance by hand instead of calling arm_rfft_fast_init_f32:
  // its size switch links the twiddle tables for every FFT length into RAM1
  // (~80KB); referencing the 256-point tables directly costs ~3KB.
  rfft.Sint = arm_cfft_sR_f32_len128;
  rfft.fftLenRFFT = FFT_SIZE;
  rfft.pTwiddleRFFT = (float32_t*)twiddleCoef_rfft_256;
  memset(prevBlock, 0, sizeof(prevBlock));
}

// Destructor implementation
AudioFilterFIRFloat::~AudioFilterFIRFloat() {
  delete[] firCoeffs;
  delete[] firState;
  delete[] partSpectra;
  delete[] fdl;
}

// setEnabled method implementation
void AudioFilterFIRFloat::setEnabled(bool enable) {
  __disable_irq();
  enabled = enable;
  __enable_irq();
}

// Takes effect at the next loadCoefficients() call
void AudioFilterFIRFloat::setFastConvolution(bool enable) {
  useFastConvolution = enable;
}

// loadCoefficients method implementation
void AudioFilterFIRFloat::loadCoefficients(const float* coeffs, uint16_t newNumTaps) {
  bool fast = useFastConvolution;

  // Step 1: Allocate and build the new engine's buffers with interrupts enabled.
  float* newCoeffs = nullptr;
  float* newState = nullptr;
  float* newPartSpectra = nullptr;
  float* newFdl = nullptr;
  uint16_t newPartitions = 0;

  if (newNumTaps > 0 && coeffs != nullptr) {
    if (fast) {
      newPartitions = (newNumTaps + AUDIO_BLOCK_SAMPLES - 1) / AUDIO_BLOCK_SAMPLES;
      newPartSpectra = new float[(size_t)newPartitions * FFT_SIZE];
      newFdl = new float[(size_t)newPartitions * FFT_SIZE]();
      if (!newPartSpectra || !newFdl) {
        // Allocation failed, do nothing and return.
        delete[] newPartSpectra;
        delete[] newFdl;
        return;
      }
      // Pre-transform each 128-tap partition, zero-padded to FFT_SIZE.
      // (arm_rfft_fast_f32 clobbers its input, hence the scratch buffer.)
      float scratch[FFT_SIZE];
      for (uint16_t p = 0; p < newPartitions; p++) {
        uint32_t offset = (uint32_t)p * AUDIO_BLOCK_SAMPLES;
        uint16_t count = newNumTaps - offset;
        if (count > AUDIO_BLOCK_SAMPLES) count = AUDIO_BLOCK_SAMPLES;
        memset(scratch, 0, sizeof(scratch));
        memcpy(scratch, coeffs + offset, count * sizeof(float));
        arm_rfft_fast_f32(&rfft, scratch, newPartSpectra + (size_t)p * FFT_SIZE, 0);
      }
    } else {
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
  }

  // Step 2: Atomically swap pointers and re-initialize the filter.
  float* oldCoeffs = nullptr;
  float* oldState = nullptr;
  float* oldPartSpectra = nullptr;
  float* oldFdl = nullptr;

  __disable_irq();
  // --- Start Critical Section ---

  // Store old pointers for later deletion
  oldCoeffs = firCoeffs;
  oldState = firState;
  oldPartSpectra = partSpectra;
  oldFdl = fdl;

  // Point to the new data
  firCoeffs = newCoeffs;
  firState = newState;
  partSpectra = newPartSpectra;
  fdl = newFdl;
  numPartitions = newPartitions;
  fdlIndex = 0;
  numTaps = newNumTaps;
  loadedFast = fast;
  memset(prevBlock, 0, sizeof(prevBlock));
  wasProcessing = false;

  // Re-initialize the CMSIS FIR instance with the new data
  if (!fast && numTaps > 0) {
    arm_fir_init_f32(&fir, numTaps, firCoeffs, firState, AUDIO_BLOCK_SAMPLES);
  }

  // --- End Critical Section ---
  __enable_irq();

  // Step 3: Free the old buffers with interrupts enabled.
  delete[] oldCoeffs;
  delete[] oldState;
  delete[] oldPartSpectra;
  delete[] oldFdl;
}

// update() method implementation
void AudioFilterFIRFloat::update(void) {
  unsigned long start_us = micros();

  audio_block_t* block = receiveReadOnly(0);
  if (!block) {
    wasProcessing = false;
    return;
  }

  // If filter is disabled or not configured, pass data through unchanged
  if (!enabled || numTaps == 0) {
    wasProcessing = false;
    transmit(block);
    release(block);
    return;
  }

  audio_block_t* outBlock = allocate();
  if (!outBlock) {
    wasProcessing = false;
    release(block);
    return;
  }

  float inputF32[AUDIO_BLOCK_SAMPLES];
  float outputF32[AUDIO_BLOCK_SAMPLES];

  arm_q15_to_float(block->data, inputF32, AUDIO_BLOCK_SAMPLES);
  release(block);

  // After a gap (bypassed, upstream stalled, or fresh coefficients) the fast
  // engine's history is stale audio - restart it from silence.
  if (loadedFast && !wasProcessing) {
    memset(fdl, 0, (size_t)numPartitions * FFT_SIZE * sizeof(float));
    memset(prevBlock, 0, sizeof(prevBlock));
    fdlIndex = 0;
  }
  wasProcessing = true;

  // No locking needed here: update() runs in the audio interrupt, and
  // loadCoefficients() swaps engine buffers with interrupts disabled.
  if (loadedFast) {
    processFast(inputF32, outputF32);
  } else {
    processDirect(inputF32, outputF32);
  }

  // Scale down the output to prevent clipping.
  for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
    outputF32[i] *= 0.5f;
  }

  arm_float_to_q15(outputF32, outBlock->data, AUDIO_BLOCK_SAMPLES);

  transmit(outBlock);
  release(outBlock);

  unsigned long elapsed_us = micros() - start_us;
  if (elapsed_us > max_update_us) {
    max_update_us = elapsed_us;
  }
}

void AudioFilterFIRFloat::processDirect(const float* input, float* output) {
  arm_fir_f32(&fir, (float32_t*)input, output, AUDIO_BLOCK_SAMPLES);
}

// Uniformly partitioned overlap-save convolution: one FFT of
// [previous block | current block], a complex multiply-accumulate of every
// filter partition against the matching entry in the frequency-domain delay
// line (partition p pairs with the input spectrum from p blocks ago), then
// one inverse FFT of which only the second half is valid output - the first
// half is circular-convolution wraparound and is discarded.
void AudioFilterFIRFloat::processFast(const float* input, float* output) {
  // FFT the new input segment directly into the newest delay line slot
  float scratch[FFT_SIZE];
  memcpy(scratch, prevBlock, AUDIO_BLOCK_SAMPLES * sizeof(float));
  memcpy(scratch + AUDIO_BLOCK_SAMPLES, input, AUDIO_BLOCK_SAMPLES * sizeof(float));
  memcpy(prevBlock, input, AUDIO_BLOCK_SAMPLES * sizeof(float));
  arm_rfft_fast_f32(&rfft, scratch, fdl + (size_t)fdlIndex * FFT_SIZE, 0);

  // CMSIS packs the two real-only bins as [DC, Nyquist, re1, im1, re2, ...],
  // so those two multiply directly and the rest are complex products.
  float acc[FFT_SIZE];
  memset(acc, 0, sizeof(acc));
  uint16_t idx = fdlIndex;
  for (uint16_t p = 0; p < numPartitions; p++) {
    const float* H = partSpectra + (size_t)p * FFT_SIZE;
    const float* X = fdl + (size_t)idx * FFT_SIZE;
    acc[0] += H[0] * X[0];
    acc[1] += H[1] * X[1];
    for (int k = 2; k < FFT_SIZE; k += 2) {
      float hr = H[k], hi = H[k + 1];
      float xr = X[k], xi = X[k + 1];
      acc[k]     += hr * xr - hi * xi;
      acc[k + 1] += hr * xi + hi * xr;
    }
    idx = (idx == 0) ? numPartitions - 1 : idx - 1;
  }
  fdlIndex = (fdlIndex + 1 == numPartitions) ? 0 : fdlIndex + 1;

  // Inverse FFT (includes the 1/N scaling); keep the valid second half
  arm_rfft_fast_f32(&rfft, acc, scratch, 1);
  memcpy(output, scratch + AUDIO_BLOCK_SAMPLES, AUDIO_BLOCK_SAMPLES * sizeof(float));
}
