#include "PEQMath.h"
#include <math.h>

// Same value as Arduino's PI define, kept local so this file has no Arduino
// dependency.
static const double PEQ_PI = 3.1415926535897932384626433832795;

static inline float clampf(float value, float lo, float hi) {
  return (value < lo) ? lo : (value > hi) ? hi : value;
}

// Cytomic/Simper trapezoidal SVF, bell configuration - matches the RBJ
// peaking EQ response exactly (see "Solving the continuous SVF equations
// using trapezoidal integration", Andrew Simper).
// The parameter ranges PEQProcessor enforces. Both the coefficient math and
// the response math apply them, so they always describe the same filter.
static void clampBandParams(float& freq, float& gain, float& q, float sampleRate) {
  float maxFreq = (20000.0f < sampleRate * 0.49f) ? 20000.0f : sampleRate * 0.49f;
  freq = clampf(freq, 20.0f, maxFreq);
  q = clampf(q, 0.1f, 10.0f);
  gain = clampf(gain, -15.0f, 15.0f);
}

// Shelf slope S = 1 in RBJ's parameterisation, i.e. alpha = sin(w0)/(2*Q)
// with Q = 1/sqrt(2).
static const double PEQ_SHELF_Q = 0.70710678118654752440;

// Shelves take the band clamps minus the Q, which is fixed above.
static void clampShelfParams(float& fc, float& gain, float sampleRate) {
  float q = PEQ_SHELF_Q;
  clampBandParams(fc, gain, q, sampleRate);
}

PeqSvfCoeffs peqComputeBellSvf(float frequency, float gain, float q, float sampleRate) {
  float freq = frequency, gc = gain, qc = q;
  clampBandParams(freq, gc, qc, sampleRate);

  // Double precision for the coefficient math only; the audio path is float32
  double A = pow(10.0, (double)gc / 40.0);
  double g = tan(PEQ_PI * (double)freq / (double)sampleRate);
  double k = 1.0 / ((double)qc * A);
  double a1 = 1.0 / (1.0 + g * (g + k));

  PeqSvfCoeffs c;
  c.a1 = (float)a1;
  c.a2 = (float)(g * a1);
  c.a3 = (float)(g * g * a1);
  c.m1 = (float)(k * (A * A - 1.0));
  return c;
}

// Exact bell (peaking EQ) magnitude in dB of the filter peqComputeBellSvf
// builds. The trapezoidal SVF is the bilinear transform of the analog
// prototype
//   H(s) = (s^2 + s*(A/Q) + 1) / (s^2 + s/(A*Q) + 1)
// with the center frequency prewarped, so its response is that prototype
// evaluated at the warped ratio tan(pi*f/fs) / tan(pi*fc/fs) instead of
// f/fc. The two agree below ~3 kHz; higher up the digital bell is narrower
// and reaches exactly 0 dB at fs/2 (1.2 dB apart at 10 kHz Q4, 2.6 dB at
// 15 kHz Q10 at 44.1 kHz) - and the headroom pad has to charge for the
// bell that runs, not the textbook one. Not on the audio path: called per
// config change, so double precision is affordable.
float calculateBellFilter(float freq, float centerFreq, float gain, float q,
                          float sampleRate) {
  if (gain == 0.0f || freq <= 0.0f || sampleRate <= 0.0f) return 0.0f; // inactive band
  if (freq >= 0.5f * sampleRate) return 0.0f;
  clampBandParams(centerFreq, gain, q, sampleRate);
  double A = pow(10.0, (double)gain / 40.0);
  double O = tan(PEQ_PI * (double)freq / (double)sampleRate) /
             tan(PEQ_PI * (double)centerFreq / (double)sampleRate);
  double c = 1.0 - O * O;
  c *= c;
  double nb = A * O / (double)q;
  double db = O / (A * (double)q);
  return (float)(10.0 * log10((c + nb * nb) / (c + db * db)));
}

// Cytomic/Simper SVF, high-shelf configuration. The sqrt(A) on g is what
// makes this the RBJ cookbook shelf and not some other one: RBJ normalises
// its prototype to w0, the SVF normalises to a corner sqrt(A) above it, and
// the two prototypes differ by exactly that substitution - so both bilinear
// transforms land on the same digital filter (verified in test_peq_math).
PeqShelfSvfCoeffs peqComputeHighShelfSvf(float fc, float gainDb, float sampleRate) {
  float freq = fc, gc = gainDb;
  clampShelfParams(freq, gc, sampleRate);

  double A = pow(10.0, (double)gc / 40.0);
  double g = tan(PEQ_PI * (double)freq / (double)sampleRate) * sqrt(A);
  double k = 1.0 / PEQ_SHELF_Q;
  double a1 = 1.0 / (1.0 + g * (g + k));

  PeqShelfSvfCoeffs c;
  c.a1 = (float)a1;
  c.a2 = (float)(g * a1);
  c.a3 = (float)(g * g * a1);
  c.m0 = (float)(A * A);
  c.m1 = (float)(k * (1.0 - A) * A);
  c.m2 = (float)(1.0 - A * A);
  return c;
}

// Exact high-shelf magnitude, the same way calculateBellFilter does it for
// the bell: the analog prototype
//   H(u) = (A^2*u^2 + (A/Q)*u + 1) / (u^2 + (1/Q)*u + 1)
// evaluated at the warped ratio tan(pi*f/fs) / (tan(pi*fc/fs)*sqrt(A)),
// which is the bilinear transform of that prototype and therefore the
// response of the filter peqComputeHighShelfSvf builds.
float highShelfDb(float freq, float fc, float gainDb, float sampleRate) {
  if (gainDb == 0.0f || freq <= 0.0f || sampleRate <= 0.0f) return 0.0f;
  float center = fc, gain = gainDb;
  clampShelfParams(center, gain, sampleRate);
  // The digital shelf reaches exactly its full gain at Nyquist
  if (freq >= 0.5f * sampleRate) return gain;

  double A = pow(10.0, (double)gain / 40.0);
  double O = tan(PEQ_PI * (double)freq / (double)sampleRate) /
             (tan(PEQ_PI * (double)center / (double)sampleRate) * sqrt(A));
  double n = 1.0 - A * A * O * O;
  double d = 1.0 - O * O;
  double nb = A * O / PEQ_SHELF_Q;
  double db = O / PEQ_SHELF_Q;
  return (float)(10.0 * log10((n * n + nb * nb) / (d * d + db * db)));
}
