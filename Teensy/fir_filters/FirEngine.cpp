#include "FirEngine.h"
#include <string.h>
#include <arm_const_structs.h>
#include <arm_common_tables.h>

// Default constructor implementation
FirEngine::FirEngine()
  : firCoeffs(nullptr),
    firState(nullptr),
    partSpectra(nullptr),
    fdl(nullptr),
    numPartitions(0),
    fdlIndex(0),
    numTaps(0),
    useFastConvolution(false),
    loadedFast(false),
    pendingValid(false)
{
  // Build the FFT instance by hand instead of calling arm_rfft_fast_init_f32:
  // its size switch links the twiddle tables for every FFT length into RAM1
  // (~80KB); referencing the 256-point tables directly costs ~3KB.
  rfft.Sint = arm_cfft_sR_f32_len128;
  rfft.fftLenRFFT = FFT_SIZE;
  rfft.pTwiddleRFFT = (float32_t*)twiddleCoef_rfft_256;
  memset(prevBlock, 0, sizeof(prevBlock));
  memset(&pending, 0, sizeof(pending));
  memset(&retired, 0, sizeof(retired));
}

// Destructor implementation
FirEngine::~FirEngine() {
  delete[] firCoeffs;
  delete[] firState;
  delete[] partSpectra;
  delete[] fdl;
  delete[] pending.coeffs;
  delete[] pending.state;
  delete[] pending.partSpectra;
  delete[] pending.fdl;
  freeRetired();
}

// Takes effect at the next coefficient load
void FirEngine::setFastConvolution(bool enable) {
  useFastConvolution = enable;
}

bool FirEngine::loadCoefficients(const float* coeffs, uint16_t newNumTaps) {
  if (!buildPending(coeffs, newNumTaps)) {
    return false;
  }
  swapPending();
  freeRetired();
  return true;
}

// Phase 1: allocate and build the new engine's buffers (interrupts enabled).
bool FirEngine::buildPending(const float* coeffs, uint16_t newNumTaps) {
  bool fast = useFastConvolution;

  float* newCoeffs = nullptr;
  float* newState = nullptr;
  float* newPartSpectra = nullptr;
  float* newFdl = nullptr;
  uint16_t newPartitions = 0;

  if (newNumTaps > 0 && coeffs != nullptr) {
    if (fast) {
      newPartitions = (newNumTaps + BLOCK_SAMPLES - 1) / BLOCK_SAMPLES;
      newPartSpectra = new float[(size_t)newPartitions * FFT_SIZE];
      newFdl = new float[(size_t)newPartitions * FFT_SIZE]();
      if (!newPartSpectra || !newFdl) {
        // Allocation failed - keep the current filter.
        delete[] newPartSpectra;
        delete[] newFdl;
        return false;
      }
      // Pre-transform each 128-tap partition, zero-padded to FFT_SIZE.
      // (arm_rfft_fast_f32 clobbers its input, hence the scratch buffer.)
      float scratch[FFT_SIZE];
      for (uint16_t p = 0; p < newPartitions; p++) {
        uint32_t offset = (uint32_t)p * BLOCK_SAMPLES;
        uint16_t count = newNumTaps - offset;
        if (count > BLOCK_SAMPLES) count = BLOCK_SAMPLES;
        memset(scratch, 0, sizeof(scratch));
        memcpy(scratch, coeffs + offset, count * sizeof(float));
        arm_rfft_fast_f32(&rfft, scratch, newPartSpectra + (size_t)p * FFT_SIZE, 0);
      }
    } else {
      newCoeffs = new float[newNumTaps];
      if (!newCoeffs) {
        // Allocation failed - keep the current filter.
        return false;
      }
      // CMSIS arm_fir expects its coefficient array in time-reversed order
      // ({b[numTaps-1], ..., b[0]}), so reverse the copy: the loaded impulse
      // response is then applied exactly as designed, matching the fast
      // convolution engine. (Symmetric linear-phase filters masked this.)
      for (uint16_t i = 0; i < newNumTaps; i++) {
        newCoeffs[i] = coeffs[newNumTaps - 1 - i];
      }

      newState = new float[newNumTaps + BLOCK_SAMPLES - 1]();
      if (!newState) {
        delete[] newCoeffs; // Clean up partial allocation
        return false;
      }
    }
  }

  pending.coeffs = newCoeffs;
  pending.state = newState;
  pending.partSpectra = newPartSpectra;
  pending.fdl = newFdl;
  pending.partitions = newPartitions;
  pending.taps = newNumTaps;
  pending.fast = fast;
  pendingValid = true;
  return true;
}

// Phase 2: swap pointers and re-initialize the filter. Fast (no allocation),
// so the caller can run it with interrupts disabled.
void FirEngine::swapPending() {
  if (!pendingValid) {
    return;
  }

  // Store old pointers for later deletion
  retired.coeffs = firCoeffs;
  retired.state = firState;
  retired.partSpectra = partSpectra;
  retired.fdl = fdl;

  // Point to the new data
  firCoeffs = pending.coeffs;
  firState = pending.state;
  partSpectra = pending.partSpectra;
  fdl = pending.fdl;
  numPartitions = pending.partitions;
  fdlIndex = 0;
  numTaps = pending.taps;
  loadedFast = pending.fast;
  memset(prevBlock, 0, sizeof(prevBlock));

  memset(&pending, 0, sizeof(pending));
  pendingValid = false;

  // Re-initialize the CMSIS FIR instance with the new data
  if (!loadedFast && numTaps > 0) {
    arm_fir_init_f32(&fir, numTaps, firCoeffs, firState, BLOCK_SAMPLES);
  }
}

// Phase 3: free the replaced buffers (interrupts enabled).
void FirEngine::freeRetired() {
  delete[] retired.coeffs;
  delete[] retired.state;
  delete[] retired.partSpectra;
  delete[] retired.fdl;
  memset(&retired, 0, sizeof(retired));
}

void FirEngine::processBlock(const float* input, float* output) {
  if (numTaps == 0) {
    // No filter loaded - pass through (the wrapper's bypass semantics)
    memmove(output, input, BLOCK_SAMPLES * sizeof(float));
    return;
  }
  if (loadedFast) {
    processFast(input, output);
  } else {
    processDirect(input, output);
  }
}

// After a gap (bypassed, upstream stalled, or fresh coefficients) the fast
// engine's history is stale audio - restart it from silence.
void FirEngine::resetHistory() {
  if (fdl != nullptr) {
    memset(fdl, 0, (size_t)numPartitions * FFT_SIZE * sizeof(float));
  }
  memset(prevBlock, 0, sizeof(prevBlock));
  fdlIndex = 0;
}

void FirEngine::processDirect(const float* input, float* output) {
  arm_fir_f32(&fir, (float32_t*)input, output, BLOCK_SAMPLES);
}

// Uniformly partitioned overlap-save convolution: one FFT of
// [previous block | current block], a complex multiply-accumulate of every
// filter partition against the matching entry in the frequency-domain delay
// line (partition p pairs with the input spectrum from p blocks ago), then
// one inverse FFT of which only the second half is valid output - the first
// half is circular-convolution wraparound and is discarded.
void FirEngine::processFast(const float* input, float* output) {
  // FFT the new input segment directly into the newest delay line slot
  float scratch[FFT_SIZE];
  memcpy(scratch, prevBlock, BLOCK_SAMPLES * sizeof(float));
  memcpy(scratch + BLOCK_SAMPLES, input, BLOCK_SAMPLES * sizeof(float));
  memcpy(prevBlock, input, BLOCK_SAMPLES * sizeof(float));
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
  memcpy(output, scratch + BLOCK_SAMPLES, BLOCK_SAMPLES * sizeof(float));
}
