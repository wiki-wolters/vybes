#ifndef AUDIO_FILTER_FIR_FLOAT_H
#define AUDIO_FILTER_FIR_FLOAT_H

#include <Arduino.h>
#include <Audio.h>
#include <arm_math.h>

// FIR filter with two interchangeable engines:
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
// next loadCoefficients() call.
class AudioFilterFIRFloat : public AudioStream {

public:
  // Default constructor
  AudioFilterFIRFloat();

  // Destructor to free allocated memory
  ~AudioFilterFIRFloat();

  void setEnabled(bool enable);

  // Select the engine used by the next loadCoefficients() call
  void setFastConvolution(bool enable);

  // The main update method, called by the Teensy Audio Library
  virtual void update(void);

  // Load new FIR coefficients. The class creates its own copy.
  void loadCoefficients(const float* coeffs, uint16_t numTaps);

  volatile unsigned long max_update_us = 0;

private:
  static const uint16_t FFT_SIZE = AUDIO_BLOCK_SAMPLES * 2;

  void processDirect(const float* input, float* output);
  void processFast(const float* input, float* output);

  audio_block_t *inputQueueArray[1];

  // Direct engine
  arm_fir_instance_f32 fir;
  float* firCoeffs;      // FIR coefficients (OWNED by this class)
  float* firState;       // FIR state buffer (OWNED by this class)

  // Fast convolution engine
  arm_rfft_fast_instance_f32 rfft;      // read-only after construction
  float* partSpectra;    // numPartitions x FFT_SIZE filter partition spectra (OWNED)
  float* fdl;            // numPartitions x FFT_SIZE input spectra history (OWNED)
  float prevBlock[AUDIO_BLOCK_SAMPLES]; // previous input block (overlap-save)
  uint16_t numPartitions;
  uint16_t fdlIndex;     // frequency-domain delay line slot of the newest block

  uint16_t numTaps;
  bool useFastConvolution; // engine for the next loadCoefficients()
  bool loadedFast;         // engine the current coefficients were built for
  bool enabled;
  bool wasProcessing;      // whether the previous update() ran the filter
};

#endif // AUDIO_FILTER_FIR_FLOAT_H
