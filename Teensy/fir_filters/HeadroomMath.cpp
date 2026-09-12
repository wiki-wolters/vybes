#include "HeadroomMath.h"

#include <math.h>

float headroomAllowanceDb(float freq) {
  if (freq <= 2000.0f) return 0.0f;
  if (freq >= 20000.0f) return 6.0f;
  return 6.0f * log10f(freq / 2000.0f); // 2k..20k spans one decade, so this hits 6 at 20k
}

// Net summed curve at one frequency: bells + crossover - allowance.
// Cascaded filters multiply in linear gain, so their dB responses add.
static float curveNetDb(float freq, const PEQBand* bands, int numBands,
                        float hpFreq, CrossoverType hpType,
                        float lpFreq, CrossoverType lpType,
                        float sampleRate) {
  float total = 0.0f;
  for (int j = 0; j < numBands; j++) {
    if (bands[j].enabled) {
      total += calculateBellFilter(freq, bands[j].frequency,
                                   bands[j].gain, bands[j].q, sampleRate);
    }
  }
  total += xoverBranchResponseDb(freq, hpFreq, hpType, true, sampleRate);
  total += xoverBranchResponseDb(freq, lpFreq, lpType, false, sampleRate);
  return total - headroomAllowanceDb(freq);
}

float headroomMaxBoostDb(const PEQBand* bands, int numBands,
                         float hpFreq, CrossoverType hpType,
                         float lpFreq, CrossoverType lpType,
                         float sampleRate) {
  float maxBoost = 0.0f;

  // Sample logarithmically from 20Hz to 20kHz
  const int numSamples = 100;
  for (int i = 0; i < numSamples; ++i) {
    float freq = 20.0f * powf(1000.0f, (float)i / (numSamples - 1));
    float net = curveNetDb(freq, bands, numBands,
                           hpFreq, hpType, lpFreq, lpType, sampleRate);
    if (net > maxBoost) maxBoost = net;
  }

  // A high-Q peak can fall between grid points (the grid step is ~7%, a
  // Q=10 bell is ~10% wide) - evaluate each enabled band's center too
  for (int j = 0; j < numBands; j++) {
    if (!bands[j].enabled) continue;
    float net = curveNetDb(bands[j].frequency, bands, numBands,
                           hpFreq, hpType, lpFreq, lpType, sampleRate);
    if (net > maxBoost) maxBoost = net;
  }

  return maxBoost;
}

float headroomMaxBoostDb(const PEQBand* bands, int numBands, float sampleRate) {
  // Branches off
  return headroomMaxBoostDb(bands, numBands,
                            0.0f, CROSSOVER_LR4, 0.0f, CROSSOVER_LR4,
                            sampleRate);
}
