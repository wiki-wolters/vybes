#ifndef PEQProcessor_h
#define PEQProcessor_h

#include <Arduino.h>
#include <AudioStream.h>
#include <arm_math.h>
#include <Audio.h>
#include "PEQMath.h"

#ifndef PI
#define PI 3.14159265359f
#endif

// Must match MAX_PEQ_POINTS on the ESP and the point limit in the WebUI
#define MAX_PEQ_BANDS 15

// PEQ Band structure
struct PEQBand {
  float frequency;
  float gain;
  float q;
  bool enabled;
};

// Animation structure (used for smooth morphs between EQ curves)
struct AnimationState {
  bool active;
  unsigned long startTime;
  unsigned long duration;
  PEQBand startBands[MAX_PEQ_BANDS];
  PEQBand targetBands[MAX_PEQ_BANDS];
  bool bandMoving[MAX_PEQ_BANDS]; // skip recomputing bands that aren't changing
};

// Multi-band parametric EQ. Every band is a "bell" (peaking) filter built on
// the Cytomic/Simper trapezoidal state-variable filter, which matches the
// standard RBJ bell response exactly and stays numerically well-behaved in
// float32 all the way down to 20Hz - so one topology serves all bands.
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
  float calculateMaxEqBoost(const PEQBand* currentBands, int numBands) const;
  void applyPreEQGain(float maxBoost, AudioAmplifier& leftAmp, AudioAmplifier& rightAmp);

  // Animation (smooth morph between curves)
  void animateToBands(const PEQBand* targetBands, int numBands, unsigned long durationMs = 50);
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
  // Cytomic SVF coefficients + per-band filter state
  struct SVFBand {
    float a1, a2, a3; // integrator coefficients
    float m1;         // bell mix coefficient
    float ic1eq, ic2eq; // integrator states
    bool active;      // enabled && gain != 0
  };

  audio_block_t *inputQueue[1];
  float sampleRate;
  bool initialized;
  bool bypassed;

  PEQBand bands[MAX_PEQ_BANDS];
  SVFBand svf[MAX_PEQ_BANDS];

  AnimationState animation;

  void updateFilter(int bandIndex);
  void processBand(int bandIndex, float32_t* buffer, int numSamples);

  void processAnimation();
  float interpolate(float start, float end, float progress);
};

// calculateBellFilter (exact bell magnitude response in dB) is declared in
// PEQMath.h, included above.

#endif
