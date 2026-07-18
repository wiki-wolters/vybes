#ifndef AUDIO_FILTER_FIR_FLOAT_H
#define AUDIO_FILTER_FIR_FLOAT_H

#include <Arduino.h>
#include <Audio.h>
#include "FirEngine.h"

// Thin AudioStream wrapper around FirEngine, which holds the actual DSP
// (direct-form CMSIS FIR and the uniformly partitioned overlap-save fast
// convolution engine - see FirEngine.h). This class only adapts the engine
// to the Teensy audio graph: q15<->float conversion, bypass, and making the
// coefficient swap atomic with respect to the audio interrupt.
class AudioFilterFIRFloat : public AudioStream {

public:
  // Default constructor
  AudioFilterFIRFloat();

  void setEnabled(bool enable);

  // Select the engine used by the next loadCoefficients() call
  void setFastConvolution(bool enable);

  // The main update method, called by the Teensy Audio Library
  virtual void update(void);

  // Load new FIR coefficients. The engine creates its own copy. Returns false
  // if buffer allocation failed (the previous filter stays loaded).
  bool loadCoefficients(const float* coeffs, uint16_t numTaps);

  volatile unsigned long max_update_us = 0;

private:
  audio_block_t *inputQueueArray[1];

  FirEngine engine;

  bool enabled;
  bool wasProcessing;      // whether the previous update() ran the filter
};

#endif // AUDIO_FILTER_FIR_FLOAT_H
