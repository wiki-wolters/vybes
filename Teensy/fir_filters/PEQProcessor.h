#ifndef PEQ_PROCESSOR_H
#define PEQ_PROCESSOR_H

#include <Audio.h>
#include "arm_math.h"

// Maximum number of EQ bands supported
#define MAX_PEQ_BANDS 15


// Structure to hold PEQ parameters
struct PEQBand {
  float frequency;  // Hz (20-20000)
  float gain;       // dB (-15 to +15)
  float q;          // Q factor (0.1 to 10)
  bool enabled;     // Enable/disable this band
};

// Animation state
struct AnimationState {
  bool active;
  unsigned long startTime;
  unsigned long duration;
  PEQBand startBands[MAX_PEQ_BANDS];
  PEQBand targetBands[MAX_PEQ_BANDS];
};



class PEQProcessor : public AudioStream {
public:
  // Constructor
  PEQProcessor();
  
  // Initialize the processor with sample rate
  void begin(float sampleRate = 44100.0f);
  
  // Set EQ parameters for a specific band
  void setBand(int bandIndex, float frequency, float gain, float q, bool enabled = true);
  
  // Set EQ parameters using PEQBand struct
  void setBand(int bandIndex, const PEQBand& band);
  
  // Update all bands from an array
  void updateBands(const PEQBand* bands, int numBands);
  
  // Enable/disable a specific band
  void enableBand(int bandIndex, bool enabled);
  
  // Clear all bands (set to unity gain)
  void clearAll();
  
  // Get current band settings
  PEQBand getBand(int bandIndex) const;
  
  // Get number of active bands
  int getActiveBandCount() const;
  
  // Animation methods
  void animateToBands(const PEQBand* targetBands, int numBands, unsigned long durationMs = 1000);
  void setAnimationSpeed(unsigned long durationMs);
  void updateAnimationState(); // Call this in your main loop to handle animations
  virtual void update(void); // Required by AudioStream
  bool isAnimating() const;
  void stopAnimation();
  
  // Bypass methods
  void setBypass(bool bypassed);
  bool isBypassed() const;
  void toggleBypass();

private:
  audio_block_t *inputQueue[1];
  PEQBand bands[MAX_PEQ_BANDS];

  // ARM DSP Biquad filter instances and state
  arm_biquad_casd_df1_inst_f32 biquad_insts[MAX_PEQ_BANDS];
  float32_t biquad_states[MAX_PEQ_BANDS][4];
  float32_t biquad_coeffs[MAX_PEQ_BANDS][5];

  AnimationState animation;
  float sampleRate;
  bool initialized;
  bool bypassed;
  
  // Internal methods
  void calculateCoefficients(const PEQBand& band, double coeffs[5]);
  void updateBiquadFilter(int bandIndex);
  void setUnityGain(int bandIndex);
  void processAnimation(); // Renamed from updateAnimation to avoid confusion
  float interpolate(float start, float end, float progress);
};

#endif