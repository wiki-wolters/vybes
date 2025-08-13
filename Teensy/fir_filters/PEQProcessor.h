#ifndef PEQProcessor_h
#define PEQProcessor_h

#include <Arduino.h>
#include <AudioStream.h>
#include <arm_math.h>

#ifndef PI
#define PI 3.14159265359f
#endif

#define MAX_PEQ_BANDS 8

// Filter type enumeration
enum FilterType {
  FILTER_BIQUAD,
  FILTER_CHAMBERLIN
};

// PEQ Band structure
struct PEQBand {
  float frequency;
  float gain;
  float q;
  bool enabled;
};

// Chamberlin state variable filter state
struct ChamberlinState {
  float low;    // Low-pass state
  float band;   // Band-pass state  
  float f;      // Frequency parameter
  float q_inv;  // 1/Q parameter
};

// Animation structure
struct AnimationState {
  bool active;
  unsigned long startTime;
  unsigned long duration;
  PEQBand startBands[MAX_PEQ_BANDS];
  PEQBand targetBands[MAX_PEQ_BANDS];
};

class PEQProcessor : public AudioStream {
public:
  PEQProcessor();
  
  // Initialization
  void begin(float sampleRate = 44100.0f);
  
  // Band control
  void setBand(int bandIndex, float frequency, float gain, float q, bool enabled = true);
  void setBand(int bandIndex, const PEQBand& band);
  void updateBands(const PEQBand* bands, int numBands);
  void enableBand(int bandIndex, bool enabled);
  void clearAll();
  
  // Band queries
  PEQBand getBand(int bandIndex) const;
  int getActiveBandCount() const;
  
  // Animation
  void animateToBands(const PEQBand* targetBands, int numBands, unsigned long durationMs = 1000);
  void setAnimationSpeed(unsigned long durationMs);
  void updateAnimationState();
  bool isAnimating() const;
  void stopAnimation();
  
  // Bypass control
  void setBypass(bool bypassed);
  bool isBypassed() const;
  void toggleBypass();
  
  // AudioStream interface
  virtual void update(void) override;

private:
  // Core member variables
  audio_block_t *inputQueue[1];
  float sampleRate;
  bool initialized;
  bool bypassed;
  
  // Band storage
  PEQBand bands[MAX_PEQ_BANDS];
  
  // Biquad filter variables
  arm_biquad_casd_df1_inst_f32 biquad_insts[MAX_PEQ_BANDS];
  float32_t biquad_coeffs[MAX_PEQ_BANDS][5];
  float32_t biquad_states[MAX_PEQ_BANDS][4];
  
  // Chamberlin filter variables
  ChamberlinState chamberlin_states[MAX_PEQ_BANDS];
  FilterType filter_types[MAX_PEQ_BANDS];
  
  // Animation
  AnimationState animation;
  
  // Filter management methods
  FilterType getOptimalFilterType(float frequency);
  void updateFilter(int bandIndex);
  void updateBiquadFilter(int bandIndex);
  void updateChamberlinFilter(int bandIndex);
  void setUnityGain(int bandIndex);
  
  // Coefficient calculation and validation
  void calculateCoefficients(const PEQBand& band, double coeffs[5]);
  bool validateCoefficients(double coeffs[5]);
  
  // Audio processing methods
  void processChamberlinBand(int bandIndex, float32_t* buffer, int numSamples);
  
  // Animation methods
  void processAnimation();
  float interpolate(float start, float end, float progress);
};

#endif