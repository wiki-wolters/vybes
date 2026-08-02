#include "MultibandCompressor.h"

// Envelope floor: silence reads as -100 dB instead of -inf
#define COMP_ENV_FLOOR 1e-5f

// Voice-priority duck smoothing (block rate): quick to engage so the first
// syllable already ducks, slow to let go so bass doesn't pump between words.
#define COMP_DUCK_ATTACK_MS 15.0f
#define COMP_DUCK_RELEASE_MS 200.0f

MultibandCompressor::MultibandCompressor()
  : AudioStream(2, inputQueueArray), sampleRate(44100.0f),
    enabled(false), solo(-1), voiceDuckDb(0.0f) {
  xoverFreq[0] = 250.0f;
  xoverFreq[1] = 4000.0f;
  for (int p = 0; p < XO_COUNT; p++) branches[p].count = 0;
  memset(xst, 0, sizeof(xst));
  resetGains();
  // Two channels x three bands of split samples per block. Heap so it lands
  // in RAM2 - RAM1/DTCM is nearly full (see the FFT twiddle table note).
  bandBuf = new float[2 * COMP_NUM_BANDS * AUDIO_BLOCK_SAMPLES];
}

MultibandCompressor::~MultibandCompressor() {
  delete[] bandBuf;
}

void MultibandCompressor::begin(float rate) {
  sampleRate = rate;
  for (int b = 0; b < COMP_NUM_BANDS; b++) {
    atkCoeff[b] = compSmoothingCoeff(params.band[b].attackMs, sampleRate);
    relCoeff[b] = compSmoothingCoeff(params.band[b].releaseMs, sampleRate);
  }
  const float blockRate = sampleRate / AUDIO_BLOCK_SAMPLES;
  voiceDuckAtk = compSmoothingCoeff(COMP_DUCK_ATTACK_MS, blockRate);
  voiceDuckRel = compSmoothingCoeff(COMP_DUCK_RELEASE_MS, blockRate);
  rebuildCrossovers();
}

void MultibandCompressor::resetGains() {
  for (int b = 0; b < COMP_NUM_BANDS; b++) {
    gain[b] = 1.0f;
    targetGain[b] = 1.0f;
    grDb[b] = 0.0f;
  }
  voiceDuckDb = 0.0f;
}

void MultibandCompressor::rebuildCrossovers() {
  XoverBranch next[XO_COUNT];
  next[XO_F1_LP] = xoverComputeBranch(xoverFreq[0], CROSSOVER_LR4, sampleRate);
  next[XO_F1_HP] = xoverComputeBranch(xoverFreq[0], CROSSOVER_LR4, sampleRate);
  next[XO_F2_LP] = xoverComputeBranch(xoverFreq[1], CROSSOVER_LR4, sampleRate);
  next[XO_F2_HP] = xoverComputeBranch(xoverFreq[1], CROSSOVER_LR4, sampleRate);
  next[XO_AP_LP] = next[XO_F2_LP];
  next[XO_AP_HP] = next[XO_F2_HP];
  // update() reads branches and states from the audio interrupt
  AudioNoInterrupts();
  memcpy(branches, next, sizeof(branches));
  memset(xst, 0, sizeof(xst));
  AudioInterrupts();
}

void MultibandCompressor::setEnabled(bool en) {
  if (en == enabled) return;
  if (en) {
    // Start transparent: silent history, unity gains
    AudioNoInterrupts();
    memset(xst, 0, sizeof(xst));
    resetGains();
    AudioInterrupts();
  }
  enabled = en;
  if (!en) resetGains(); // meters read zero while bypassed
}

void MultibandCompressor::setCrossovers(float f1, float f2) {
  // Keep the splits ordered and inside the audible range; CrossoverMath
  // clamps each branch further.
  f1 = constrain(f1, 40.0f, 1000.0f);
  f2 = constrain(f2, 2.0f * f1, 12000.0f);
  xoverFreq[0] = f1;
  xoverFreq[1] = f2;
  rebuildCrossovers();
}

void MultibandCompressor::setBand(int idx, float thresholdDb, float ratio,
                                  float attackMs, float releaseMs, float makeupDb) {
  if (idx < 0 || idx >= COMP_NUM_BANDS) return;
  CompBandParams& b = params.band[idx];
  b.thresholdDb = constrain(thresholdDb, -60.0f, 0.0f);
  b.ratio = constrain(ratio, 1.0f, 20.0f);
  b.attackMs = constrain(attackMs, 0.5f, 500.0f);
  b.releaseMs = constrain(releaseMs, 10.0f, 2000.0f);
  b.makeupDb = constrain(makeupDb, -12.0f, 12.0f);
  atkCoeff[idx] = compSmoothingCoeff(b.attackMs, sampleRate);
  relCoeff[idx] = compSmoothingCoeff(b.releaseMs, sampleRate);
}

void MultibandCompressor::setBandBypass(int idx, bool bypass) {
  if (idx < 0 || idx >= COMP_NUM_BANDS) return;
  params.band[idx].bypass = bypass;
}

void MultibandCompressor::setSolo(int idx) {
  solo = (idx >= 0 && idx < COMP_NUM_BANDS) ? idx : -1;
}

void MultibandCompressor::setStrength(float pct) {
  params.strength = constrain(pct, 0.0f, 100.0f) / 100.0f;
}

void MultibandCompressor::setVoicePriority(float db) {
  params.voicePriorityDb = constrain(db, 0.0f, 24.0f);
}

// Run one branch cascade on a single sample
static inline float runBranchHp(const XoverBranch& br, XoverSectionState* st, float v) {
  for (int s = 0; s < br.count; s++) v = xoverProcessHighpass(br.section[s], st[s], v);
  return v;
}
static inline float runBranchLp(const XoverBranch& br, XoverSectionState* st, float v) {
  for (int s = 0; s < br.count; s++) v = xoverProcessLowpass(br.section[s], st[s], v);
  return v;
}

void MultibandCompressor::update(void) {
  audio_block_t* in[2] = { receiveReadOnly(0), receiveReadOnly(1) };

  if (!enabled) {
    for (int ch = 0; ch < 2; ch++) {
      if (in[ch]) {
        transmit(in[ch], ch);
        release(in[ch]);
      }
    }
    return;
  }
  if (!bandBuf) return;

  // Pass 1: split both channels into bands, tracking the block peak per band
  float peak[COMP_NUM_BANDS] = {0.0f, 0.0f, 0.0f};
  for (int ch = 0; ch < 2; ch++) {
    float inBuf[AUDIO_BLOCK_SAMPLES];
    if (in[ch]) {
      arm_q15_to_float(in[ch]->data, inBuf, AUDIO_BLOCK_SAMPLES);
    } else {
      memset(inBuf, 0, sizeof(inBuf));
    }
    float* bands = bandBuf + ch * COMP_NUM_BANDS * AUDIO_BLOCK_SAMPLES;
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
      const float x = inBuf[i];
      const float bassRaw = runBranchLp(branches[XO_F1_LP], xst[ch][XO_F1_LP], x);
      const float high = runBranchHp(branches[XO_F1_HP], xst[ch][XO_F1_HP], x);
      const float mid = runBranchLp(branches[XO_F2_LP], xst[ch][XO_F2_LP], high);
      const float treble = runBranchHp(branches[XO_F2_HP], xst[ch][XO_F2_HP], high);
      // LR4 allpass at f2 keeps bass phase-aligned with mid+treble
      const float bass = runBranchLp(branches[XO_AP_LP], xst[ch][XO_AP_LP], bassRaw)
                       + runBranchHp(branches[XO_AP_HP], xst[ch][XO_AP_HP], bassRaw);
      bands[0 * AUDIO_BLOCK_SAMPLES + i] = bass;
      bands[1 * AUDIO_BLOCK_SAMPLES + i] = mid;
      bands[2 * AUDIO_BLOCK_SAMPLES + i] = treble;
      float a;
      a = fabsf(bass);   if (a > peak[0]) peak[0] = a;
      a = fabsf(mid);    if (a > peak[1]) peak[1] = a;
      a = fabsf(treble); if (a > peak[2]) peak[2] = a;
    }
  }

  // Control tick (block rate, ~2.9ms): static curve -> per-band target gain
  float envDb[COMP_NUM_BANDS];
  for (int b = 0; b < COMP_NUM_BANDS; b++) {
    envDb[b] = 20.0f * log10f(peak[b] > COMP_ENV_FLOOR ? peak[b] : COMP_ENV_FLOOR);
  }
  const float duckTarget =
      params.voicePriorityDb * compVoiceActivity(envDb[1], params.band[1].thresholdDb);
  const float duckCoeff = duckTarget > voiceDuckDb ? voiceDuckAtk : voiceDuckRel;
  voiceDuckDb += duckCoeff * (duckTarget - voiceDuckDb);

  float grNow[COMP_NUM_BANDS];
  compComputeTargets(params, envDb, voiceDuckDb, targetGain, grNow);

  // Pass 2: ramp gains per sample and recombine
  audio_block_t* out[2] = { allocate(), allocate() };
  const int soloBand = solo;
  for (int ch = 0; ch < 2; ch++) {
    if (!out[ch]) continue;
    const float* bands = bandBuf + ch * COMP_NUM_BANDS * AUDIO_BLOCK_SAMPLES;
    float g[COMP_NUM_BANDS] = { gain[0], gain[1], gain[2] };
    float outBuf[AUDIO_BLOCK_SAMPLES];
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
      float acc = 0.0f;
      for (int b = 0; b < COMP_NUM_BANDS; b++) {
        const float coeff = targetGain[b] < g[b] ? atkCoeff[b] : relCoeff[b];
        g[b] += coeff * (targetGain[b] - g[b]);
        if (soloBand < 0 || soloBand == b) {
          acc += bands[b * AUDIO_BLOCK_SAMPLES + i] * g[b];
        }
      }
      outBuf[i] = acc;
    }
    // Both channels ramp identically from the same start gains; persist
    // the state once, after the second channel.
    if (ch == 1) {
      for (int b = 0; b < COMP_NUM_BANDS; b++) gain[b] = g[b];
    }
    arm_float_to_q15(outBuf, out[ch]->data, AUDIO_BLOCK_SAMPLES);
  }

  // Report the reduction actually applied (smoothed gain vs scaled makeup)
  for (int b = 0; b < COMP_NUM_BANDS; b++) {
    const float makeup = params.band[b].bypass ? 0.0f
                       : params.band[b].makeupDb * params.strength;
    const float applied = makeup - 20.0f * log10f(gain[b] > 1e-6f ? gain[b] : 1e-6f);
    grDb[b] = applied > 0.0f ? applied : 0.0f;
  }

  for (int ch = 0; ch < 2; ch++) {
    if (in[ch]) release(in[ch]);
    if (out[ch]) {
      transmit(out[ch], ch);
      release(out[ch]);
    }
  }
}
