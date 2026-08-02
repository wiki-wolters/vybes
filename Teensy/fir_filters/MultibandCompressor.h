#ifndef MULTIBAND_COMPRESSOR_H
#define MULTIBAND_COMPRESSOR_H

#include <Arduino.h>
#include <Audio.h>
#include <arm_math.h>
#include "CrossoverMath.h"
#include "CompressorMath.h"

// Stereo-linked 3-band compressor for the mixed input bus. Sits between the
// shared input EQ and the eight output source mixers, so every output hears
// the same compressed program.
//
// Topology per channel (all LR4, reusing the crossover SVF math):
//   bass   = LP4(f1), then through an LR4 allpass at f2 (LP+HP sum) so it
//            stays phase-matched with mid+treble when recombined
//   mid    = HP4(f1) -> LP4(f2)
//   treble = HP4(f1) -> HP4(f2)
//
// Detection is stereo-linked (max of |L|,|R| per band) with block-rate
// control: the static curve computes a target gain every 128 samples and a
// per-sample one-pole ramps toward it at the band's attack/release rate.
// Bypass lives inside the object - disabled, blocks pass through untouched.
class MultibandCompressor : public AudioStream {
public:
  MultibandCompressor();
  ~MultibandCompressor();

  void begin(float sampleRate);

  // All setters are safe to call from loop context.
  void setEnabled(bool enabled);
  void setCrossovers(float f1, float f2);
  void setBand(int idx, float thresholdDb, float ratio,
               float attackMs, float releaseMs, float makeupDb);
  void setBandBypass(int idx, bool bypass);
  void setSolo(int idx); // 0..2 audits one band, -1 = normal
  void setStrength(float pct);        // 0..100
  void setVoicePriority(float db);    // 0..12 typical

  bool isEnabled() const { return enabled; }
  // Currently applied reduction, dB >= 0 (for GRM meter frames)
  float gainReductionDb(int band) const {
    return (band >= 0 && band < COMP_NUM_BANDS) ? grDb[band] : 0.0f;
  }

  virtual void update(void) override;

private:
  // Split-path indices into branches[]/xst[][]
  enum { XO_F1_LP, XO_F1_HP, XO_F2_LP, XO_F2_HP, XO_AP_LP, XO_AP_HP, XO_COUNT };

  void rebuildCrossovers();
  void resetGains();

  audio_block_t* inputQueueArray[2];
  float sampleRate;

  volatile bool enabled;
  CompParams params;
  volatile int solo; // -1 = off

  float xoverFreq[2]; // f1 (bass/mid), f2 (mid/treble)
  XoverBranch branches[XO_COUNT];
  XoverSectionState xst[2][XO_COUNT][2]; // [channel][path][section]

  // Control state (written at block rate in update, read by loop for GRM)
  float gain[COMP_NUM_BANDS];       // smoothed linear gain per band
  float targetGain[COMP_NUM_BANDS];
  float atkCoeff[COMP_NUM_BANDS];   // per-sample one-pole coefficients
  float relCoeff[COMP_NUM_BANDS];
  float voiceDuckDb;                // smoothed bass duck amount
  float voiceDuckAtk, voiceDuckRel; // block-rate coefficients
  volatile float grDb[COMP_NUM_BANDS];

  float* bandBuf; // heap (RAM2): [2 channels][3 bands][AUDIO_BLOCK_SAMPLES]
};

#endif // MULTIBAND_COMPRESSOR_H
