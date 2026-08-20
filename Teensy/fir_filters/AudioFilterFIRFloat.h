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

  // Same, streaming the coefficients in from a feed - what SD loads use, so
  // no filter-sized copy exists alongside the engine's buffers. Also returns
  // false if the feed came up short (see FirEngine::buildPending).
  bool loadCoefficients(CoeffFeed& feed, uint16_t numTaps);

  // The same load split in two, so a caller loading several filters can
  // claim every buffer before any file I/O happens - see the header note on
  // FirEngine::reservePending. reserveCoefficients allocates; fillReserved
  // reads the feed in and commits; discardReservation drops an unused one.
  bool reserveCoefficients(uint16_t numTaps);
  bool fillReserved(CoeffFeed& feed);
  void discardReservation();

  // Floats reserveCoefficientsIn needs for numTaps, and the reservation
  // itself against caller-owned storage - see FirEngine::reservePendingIn.
  size_t reservedFloats(uint16_t numTaps) const;
  bool reserveCoefficientsIn(float* storage, uint16_t numTaps);

  volatile unsigned long max_update_us = 0;

private:
  // Phases 2 and 3 of a load: swap the freshly built buffers in with the
  // audio interrupt held off, then free the replaced ones.
  void commitLoad();

  audio_block_t *inputQueueArray[1];

  FirEngine engine;

  bool enabled;
  bool wasProcessing;      // whether the previous update() ran the filter
};

#endif // AUDIO_FILTER_FIR_FLOAT_H
