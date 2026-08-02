#include "CompressorMath.h"
#include <math.h>

float compCurveGrDb(float envDb, float thresholdDb, float ratio) {
  if (ratio <= 1.0f) return 0.0f;
  const float slope = 1.0f - 1.0f / ratio;
  const float over = envDb - thresholdDb;
  const float halfKnee = COMP_KNEE_DB * 0.5f;
  if (over <= -halfKnee) return 0.0f;
  if (over >= halfKnee) return slope * over;
  // Quadratic blend across the knee: matches value and slope at both ends
  const float x = over + halfKnee;
  return slope * x * x / (2.0f * COMP_KNEE_DB);
}

float compVoiceActivity(float envMidDb, float midThresholdDb) {
  const float t = (envMidDb - midThresholdDb) / COMP_KNEE_DB;
  if (t <= 0.0f) return 0.0f;
  if (t >= 1.0f) return 1.0f;
  return t;
}

void compComputeTargets(const CompParams& p, const float* envDb,
                        float voiceDuckDb, float* outGainLin, float* outGrDb) {
  for (int b = 0; b < COMP_NUM_BANDS; b++) {
    if (p.band[b].bypass) {
      outGrDb[b] = 0.0f;
      outGainLin[b] = 1.0f;
      continue;
    }
    float threshold = p.band[b].thresholdDb;
    if (b == 0) threshold -= voiceDuckDb; // duck bass while voice is active
    const float gr = compCurveGrDb(envDb[b], threshold, p.band[b].ratio) * p.strength;
    const float makeup = p.band[b].makeupDb * p.strength;
    outGrDb[b] = gr;
    outGainLin[b] = powf(10.0f, (makeup - gr) / 20.0f);
  }
}

float compSmoothingCoeff(float ms, float rateHz) {
  if (ms <= 0.0f) return 1.0f;
  return 1.0f - expf(-1000.0f / (ms * rateHz));
}
