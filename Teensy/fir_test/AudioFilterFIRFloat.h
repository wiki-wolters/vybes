#ifndef AUDIO_FILTER_FIR_FLOAT_H
#define AUDIO_FILTER_FIR_FLOAT_H

#include <Arduino.h>
#include <Audio.h>
#include <arm_math.h>

class AudioFilterFIRFloat : public AudioStream {

public:
  // Default constructor
  AudioFilterFIRFloat();
  
  // Destructor to free allocated memory
  ~AudioFilterFIRFloat();

  void setEnabled(bool enable);

  // The main update method, called by the Teensy Audio Library
  virtual void update(void);

  // Load new FIR coefficients. The class creates its own copy.
  void loadCoefficients(const float* coeffs, uint16_t numTaps);

private:
  audio_block_t *inputQueueArray[1];
  arm_fir_instance_f32 fir;
  float* firCoeffs;      // Pointer to the FIR coefficients (OWNED by this class)
  float* firState;       // Pointer to the FIR state buffer (OWNED by this class)
  uint16_t numTaps;
  bool enabled;
};

#endif // AUDIO_FILTER_FIR_FLOAT_H
