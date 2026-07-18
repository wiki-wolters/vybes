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
PeqSvfCoeffs peqComputeBellSvf(float frequency, float gain, float q, float sampleRate) {
  float maxFreq = (20000.0f < sampleRate * 0.49f) ? 20000.0f : sampleRate * 0.49f;
  float freq = clampf(frequency, 20.0f, maxFreq);
  float qc = clampf(q, 0.1f, 10.0f);
  float gc = clampf(gain, -15.0f, 15.0f);

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

// Exact bell (peaking EQ) magnitude in dB, from the RBJ analog prototype:
//   H(s) = (s^2 + s*(A/Q) + 1) / (s^2 + s/(A*Q) + 1), evaluated at s = j*w/w0
// The SVF above is the bilinear (frequency-warped) realisation of the same
// prototype, so this matches the audible response.
float calculateBellFilter(float freq, float centerFreq, float gain, float q) {
  if (gain == 0.0f || q <= 0.0f || centerFreq <= 0.0f || freq <= 0.0f) return 0.0f;
  float A = powf(10.0f, gain / 40.0f);
  float O = freq / centerFreq;
  float c = 1.0f - O * O;
  c *= c;
  float nb = A * O / q;
  float db = O / (A * q);
  float num = c + nb * nb;
  float den = c + db * db;
  return 10.0f * log10f(num / den);
}
