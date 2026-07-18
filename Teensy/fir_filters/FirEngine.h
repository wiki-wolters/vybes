#ifndef FIR_ENGINE_H
#define FIR_ENGINE_H

#include <stdint.h>
#include <arm_math.h>

// Hardware-free core of the FIR filter, with two interchangeable engines:
//
//  - Direct form: CMSIS arm_fir_f32. Cost grows linearly with tap count, so
//    three channels max out the CPU around 2000 taps.
//
//  - Fast convolution: uniformly partitioned overlap-save. The filter is
//    split into 128-tap partitions, each pre-transformed with a 256-point
//    real FFT at load time. Per audio block the input is FFT'd once and
//    multiplied against every partition through a frequency-domain delay
//    line, so cost grows only slightly with tap count. Output is sample
//    identical to the direct engine (no added latency).
//
// The engine is chosen with setFastConvolution() and takes effect at the
// next coefficient load. This class has no AudioStream/Arduino dependencies
// so it can be exercised host-side; AudioFilterFIRFloat wraps it into the
// Teensy audio graph.
class FirEngine {

public:
  static const uint16_t BLOCK_SAMPLES = 128;
  static const uint16_t FFT_SIZE = BLOCK_SAMPLES * 2;

  FirEngine();

  // Destructor to free allocated memory
  ~FirEngine();

  // Select the engine used by the next coefficient load
  void setFastConvolution(bool enable);

  // Load new FIR coefficients (the engine keeps its own copy). Returns false
  // if buffer allocation failed (the previous filter stays loaded). This is
  // buildPending() + swapPending() + freeRetired() in one call; callers that
  // process audio from an interrupt should use the three phases directly so
  // only the pointer swap runs inside their critical section.
  bool loadCoefficients(const float* coeffs, uint16_t numTaps);

  // Three-phase coefficient load:
  //  1. buildPending: allocate and pre-transform the new engine buffers
  //     (slow; run with interrupts enabled). Returns false on allocation
  //     failure, in which case nothing is pending and the current filter
  //     is untouched.
  //  2. swapPending: swap the pending buffers in and re-initialize the
  //     filter (fast pointer swap; run with interrupts disabled).
  //  3. freeRetired: release the replaced buffers (run with interrupts
  //     enabled).
  bool buildPending(const float* coeffs, uint16_t numTaps);
  void swapPending();
  void freeRetired();

  // Process one BLOCK_SAMPLES-sample block through the loaded filter. With
  // no filter loaded (taps() == 0) the input is copied through unchanged,
  // matching the wrapper's bypass semantics.
  void processBlock(const float* input, float* output);

  // Restart the fast-convolution history from silence. Called after a gap in
  // processing (bypassed, upstream stalled) so stale audio isn't convolved.
  void resetHistory();

  uint16_t taps() const { return numTaps; }
  bool fastLoaded() const { return loadedFast; }

private:
  void processDirect(const float* input, float* output);
  void processFast(const float* input, float* output);

  // One engine's worth of coefficient/state buffers, so the pending and
  // retired sets can be carried between the load phases.
  struct Buffers {
    float* coeffs;       // direct: FIR coefficients
    float* state;        // direct: FIR state buffer
    float* partSpectra;  // fast: numPartitions x FFT_SIZE filter partition spectra
    float* fdl;          // fast: numPartitions x FFT_SIZE input spectra history
    uint16_t partitions;
    uint16_t taps;
    bool fast;
  };

  // Direct engine
  arm_fir_instance_f32 fir;
  float* firCoeffs;      // FIR coefficients (OWNED by this class)
  float* firState;       // FIR state buffer (OWNED by this class)

  // Fast convolution engine
  arm_rfft_fast_instance_f32 rfft;      // read-only after construction
  float* partSpectra;    // numPartitions x FFT_SIZE filter partition spectra (OWNED)
  float* fdl;            // numPartitions x FFT_SIZE input spectra history (OWNED)
  float prevBlock[BLOCK_SAMPLES]; // previous input block (overlap-save)
  uint16_t numPartitions;
  uint16_t fdlIndex;     // frequency-domain delay line slot of the newest block

  uint16_t numTaps;
  bool useFastConvolution; // engine for the next coefficient load
  bool loadedFast;         // engine the current coefficients were built for

  Buffers pending;         // built by buildPending, consumed by swapPending
  Buffers retired;         // produced by swapPending, freed by freeRetired
  bool pendingValid;
};

#endif // FIR_ENGINE_H
