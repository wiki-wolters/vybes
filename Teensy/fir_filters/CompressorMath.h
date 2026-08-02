#ifndef COMPRESSOR_MATH_H
#define COMPRESSOR_MATH_H

// Pure control math for the mixed-input multiband compressor, shared by
// MultibandCompressor (on the Teensy) and the host-native test suite - no
// Arduino/Audio dependencies. The audio-rate work (band splitting, gain
// smoothing) lives in MultibandCompressor; everything that decides *how
// much* gain to apply is here so it can be unit-tested.

#define COMP_NUM_BANDS 3

// Soft knee width. Fixed rather than a parameter: one less knob, and 6 dB
// is a safe musical default for program material.
#define COMP_KNEE_DB 6.0f

struct CompBandParams {
  float thresholdDb = -24.0f;
  float ratio = 2.0f;        // >= 1; 1 = no compression
  float attackMs = 10.0f;    // gain smoothing when reducing
  float releaseMs = 150.0f;  // gain smoothing when recovering
  float makeupDb = 0.0f;
  bool bypass = false;
};

struct CompParams {
  CompBandParams band[COMP_NUM_BANDS]; // 0 = bass, 1 = mid/voice, 2 = treble
  float strength = 1.0f;               // 0..1, scales reduction and makeup
  float voicePriorityDb = 0.0f;        // extra bass duck while mid is active
};

// Gain reduction (dB, >= 0) of the static curve: hard 1:ratio slope above
// the threshold, blended quadratically across a COMP_KNEE_DB-wide knee.
float compCurveGrDb(float envDb, float thresholdDb, float ratio);

// How "active" the voice band is, 0..1: fades in linearly as the mid-band
// envelope rises from its threshold to threshold + COMP_KNEE_DB.
float compVoiceActivity(float envMidDb, float midThresholdDb);

// Per-band control tick: fold strength, bypass, makeup and the (already
// smoothed) voice-priority bass duck into a target linear gain and a
// reported gain reduction per band. envDb, outGainLin and outGrDb each
// have COMP_NUM_BANDS entries.
void compComputeTargets(const CompParams& p, const float* envDb,
                        float voiceDuckDb, float* outGainLin, float* outGrDb);

// One-pole smoothing coefficient (0..1) for a time constant in ms at a
// given update rate. ms <= 0 returns 1 (jump immediately).
float compSmoothingCoeff(float ms, float rateHz);

#endif // COMPRESSOR_MATH_H
