#include "DynamicEqMath.h"
#include <math.h>

float volumeLinearToDb(float linear) {
  if (linear <= 0.0f) return VOLUME_FLOOR_DB;
  float db = 20.0f * log10f(linear);
  return (db < VOLUME_FLOOR_DB) ? VOLUME_FLOOR_DB : db;
}

float volumePctToDb(int pct) {
  if (pct <= 0) return VOLUME_FLOOR_DB;
  // 20*log10((pct/100)^3) - the cube is the sketch's volume law (setVolume)
  float db = 60.0f * log10f((float)pct / 100.0f);
  return (db < VOLUME_FLOOR_DB) ? VOLUME_FLOOR_DB : db;
}

void dynamicEqGains(const PEQBand* ref, const float* loudGain, int n,
                    float refDb, float loudDb, bool hasLoud, float volDb,
                    float* outGain) {
  // Below the reference the curve does not change - the loudness shelves
  // carry that half of the feature - so the only interpolation is upward.
  float t = 0.0f;
  if (hasLoud && loudDb > refDb && volDb > refDb) {
    t = (volDb - refDb) / (loudDb - refDb);
    if (t > 1.0f) t = 1.0f; // hold the loud anchor past it
  }

  for (int i = 0; i < n; i++) {
    outGain[i] = ref[i].gain + t * (loudGain[i] - ref[i].gain);
  }
}

float loudnessDropDb(float refDb, float volDb) {
  return (refDb > volDb) ? (refDb - volDb) : 0.0f;
}

float loudnessShelfGainDb(float dropDb) {
  if (dropDb <= 0.0f) return 0.0f;
  float gain = LOUDNESS_SLOPE_DB_PER_DB * dropDb;
  if (gain > LOUDNESS_MAX_DB) gain = LOUDNESS_MAX_DB;
  return -gain;
}

float loudnessCompensationDb(float freq, float dropDb, float sampleRate) {
  float gain = loudnessShelfGainDb(dropDb);
  if (gain == 0.0f) return 0.0f;
  return highShelfDb(freq, LOUDNESS_SHELF1_HZ, gain, sampleRate) +
         highShelfDb(freq, LOUDNESS_SHELF2_HZ, gain, sampleRate);
}
