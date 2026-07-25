#ifndef CROSSOVER_FILTER_H
#define CROSSOVER_FILTER_H

#include <Arduino.h>
#include <Audio.h>
#include <arm_math.h>
#include "CrossoverMath.h"

// Per-output HP + LP crossover: up to two SVF sections per branch, processed
// in float32 (see CrossoverMath.h for the section math and type table).
// Bypass lives inside the object - with both branches off, blocks pass
// through untouched - so the audio graph never rewires patchcords.
class CrossoverFilter : public AudioStream {
public:
  CrossoverFilter();

  void begin(float sampleRate);

  // Reconfigure one branch. freq 0 (or negative) turns the branch off.
  // Safe to call from loop context; the swap is fenced from the audio
  // interrupt and the branch restarts from silent integrators.
  void setHighpass(float freq, CrossoverType type);
  void setLowpass(float freq, CrossoverType type);

  virtual void update(void) override;

private:
  void applyBranch(XoverBranch& target, XoverSectionState* states,
                   float freq, CrossoverType type);

  audio_block_t* inputQueueArray[1];
  float sampleRate;

  XoverBranch hp, lp;
  XoverSectionState hpState[2], lpState[2];
};

#endif // CROSSOVER_FILTER_H
