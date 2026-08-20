#include "FirEngine.h"
#include <string.h>
#include <new>
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
    loadedOwned(false),
    pendingReserved(false),
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
  if (loadedOwned) {
    delete[] firCoeffs;
    delete[] firState;
    delete[] partSpectra;
    delete[] fdl;
  }
  discardPending();
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

bool FirEngine::loadCoefficients(CoeffFeed& feed, uint16_t newNumTaps) {
  if (!buildPending(feed, newNumTaps)) {
    return false;
  }
  swapPending();
  freeRetired();
  return true;
}

// Adapter so the array-taking entry points share the one build path below.
namespace {
class ArrayCoeffFeed : public CoeffFeed {
public:
  ArrayCoeffFeed(const float* src, uint16_t taps) : src(src), left(taps) {}
  uint16_t read(float* dst, uint16_t count) override {
    if (count > left) count = left;
    if (count == 0) return 0; // src may be null for a clear
    memcpy(dst, src, (size_t)count * sizeof(float));
    src += count;
    left -= count;
    return count;
  }
private:
  const float* src;
  uint16_t left;
};
} // namespace

bool FirEngine::buildPending(const float* coeffs, uint16_t newNumTaps) {
  // A null array is a clear, whatever tap count came with it.
  if (coeffs == nullptr) newNumTaps = 0;
  ArrayCoeffFeed feed(coeffs, newNumTaps);
  return buildPending(feed, newNumTaps);
}

// Phase 1: allocate and build the new engine's buffers (interrupts enabled).
// Reserve-then-fill in one call; see the header on why a caller loading a
// whole set of filters should use the two halves separately instead.
bool FirEngine::buildPending(CoeffFeed& feed, uint16_t newNumTaps) {
  if (!reservePending(newNumTaps)) {
    return false;
  }
  return fillPending(feed);
}

// Phase 1a: allocation only. Nothing here reads, opens or formats anything,
// so a caller can put every filter's reservation back-to-back and have the
// partition arrays land in one contiguous stretch of heap.
bool FirEngine::reservePending(uint16_t newNumTaps) {
  discardPending();

  pending.fast = useFastConvolution;
  pending.taps = newNumTaps;
  pending.owned = true;

  if (newNumTaps > 0) {
    // nothrow: the Teensy core's operator new returns nullptr rather than
    // throwing, but the compiler assumes throwing-new can't - without
    // std::nothrow these null checks are dead code (memset to address 0 =
    // hard fault, the crash 82b8bd8 tried to fix). The zeroing that
    // value-init used to do is explicit instead.
    if (pending.fast) {
      pending.partitions = (newNumTaps + BLOCK_SAMPLES - 1) / BLOCK_SAMPLES;
      size_t span = (size_t)pending.partitions * FFT_SIZE;
      pending.partSpectra = new (std::nothrow) float[span];
      pending.fdl = new (std::nothrow) float[span];
      if (!pending.partSpectra || !pending.fdl) {
        discardPending(); // Allocation failed - keep the current filter
        return false;
      }
      memset(pending.fdl, 0, span * sizeof(float));
    } else {
      pending.coeffs = new (std::nothrow) float[newNumTaps];
      pending.state = new (std::nothrow) float[newNumTaps + BLOCK_SAMPLES - 1];
      if (!pending.coeffs || !pending.state) {
        discardPending(); // Allocation failed - keep the current filter
        return false;
      }
      memset(pending.state, 0,
             (size_t)(newNumTaps + BLOCK_SAMPLES - 1) * sizeof(float));
    }
  }

  pendingReserved = true;
  return true;
}

size_t FirEngine::pendingFloats(uint16_t numTaps) const {
  if (numTaps == 0) return 0;
  if (useFastConvolution) {
    uint16_t parts = (numTaps + BLOCK_SAMPLES - 1) / BLOCK_SAMPLES;
    return (size_t)parts * FFT_SIZE * 2; // partition spectra + delay line
  }
  return (size_t)numTaps                      // coefficients
       + (size_t)numTaps + BLOCK_SAMPLES - 1; // filter state
}

// Phase 1a against storage the caller owns. Same layout as reservePending,
// carved out of one block instead of allocated per buffer.
bool FirEngine::reservePendingIn(float* storage, uint16_t newNumTaps) {
  discardPending();

  pending.fast = useFastConvolution;
  pending.taps = newNumTaps;
  pending.owned = false;

  if (newNumTaps > 0) {
    if (storage == nullptr) {
      discardPending();
      return false;
    }
    if (pending.fast) {
      pending.partitions = (newNumTaps + BLOCK_SAMPLES - 1) / BLOCK_SAMPLES;
      size_t span = (size_t)pending.partitions * FFT_SIZE;
      pending.partSpectra = storage;
      pending.fdl = storage + span;
      memset(pending.fdl, 0, span * sizeof(float));
    } else {
      pending.coeffs = storage;
      pending.state = storage + newNumTaps;
      memset(pending.state, 0,
             (size_t)(newNumTaps + BLOCK_SAMPLES - 1) * sizeof(float));
    }
  }

  pendingReserved = true;
  return true;
}

// Phase 1b: pull the coefficients into the reserved buffers. Exactly one
// pass over the feed, in order, so nothing filter-sized is needed on the
// side: the fast engine transforms each 128-tap partition straight out of
// the stack scratch it already used for zero-padding, and the direct engine
// reads into the array it had to allocate anyway.
bool FirEngine::fillPending(CoeffFeed& feed) {
  if (!pendingReserved) {
    return false;
  }

  if (pending.taps > 0) {
    if (pending.fast) {
      // Pre-transform each 128-tap partition, zero-padded to FFT_SIZE.
      // (arm_rfft_fast_f32 clobbers its input, hence the scratch buffer.)
      float scratch[FFT_SIZE];
      for (uint16_t p = 0; p < pending.partitions; p++) {
        uint32_t offset = (uint32_t)p * BLOCK_SAMPLES;
        uint16_t count = pending.taps - offset;
        if (count > BLOCK_SAMPLES) count = BLOCK_SAMPLES;
        memset(scratch, 0, sizeof(scratch));
        if (feed.read(scratch, count) != count) {
          // The feed ran dry mid-filter: a half-built filter is not a
          // shorter one, so keep the current one and let the caller report
          // a read failure rather than an allocation failure.
          discardPending();
          return false;
        }
        arm_rfft_fast_f32(&rfft, scratch, pending.partSpectra + (size_t)p * FFT_SIZE, 0);
      }
    } else {
      if (feed.read(pending.coeffs, pending.taps) != pending.taps) {
        discardPending(); // See the fast engine's short-feed note above
        return false;
      }
      // CMSIS arm_fir expects its coefficient array in time-reversed order
      // ({b[numTaps-1], ..., b[0]}), so reverse in place: the loaded impulse
      // response is then applied exactly as designed, matching the fast
      // convolution engine. (Symmetric linear-phase filters masked this.)
      for (uint16_t i = 0; i < pending.taps / 2; i++) {
        float t = pending.coeffs[i];
        pending.coeffs[i] = pending.coeffs[pending.taps - 1 - i];
        pending.coeffs[pending.taps - 1 - i] = t;
      }
    }
  }

  pendingValid = true;
  return true;
}

// Release a reservation that will not be swapped in.
void FirEngine::discardPending() {
  if (pending.owned) {
    delete[] pending.coeffs;
    delete[] pending.state;
    delete[] pending.partSpectra;
    delete[] pending.fdl;
  }
  memset(&pending, 0, sizeof(pending));
  pendingReserved = false;
  pendingValid = false;
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
  retired.owned = loadedOwned;

  // Point to the new data
  firCoeffs = pending.coeffs;
  firState = pending.state;
  partSpectra = pending.partSpectra;
  fdl = pending.fdl;
  numPartitions = pending.partitions;
  fdlIndex = 0;
  numTaps = pending.taps;
  loadedFast = pending.fast;
  loadedOwned = pending.owned;
  memset(prevBlock, 0, sizeof(prevBlock));

  memset(&pending, 0, sizeof(pending));
  pendingReserved = false;
  pendingValid = false;

  // Re-initialize the CMSIS FIR instance with the new data
  if (!loadedFast && numTaps > 0) {
    arm_fir_init_f32(&fir, numTaps, firCoeffs, firState, BLOCK_SAMPLES);
  }
}

// Phase 3: free the replaced buffers (interrupts enabled).
void FirEngine::freeRetired() {
  if (retired.owned) {
    delete[] retired.coeffs;
    delete[] retired.state;
    delete[] retired.partSpectra;
    delete[] retired.fdl;
  }
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
