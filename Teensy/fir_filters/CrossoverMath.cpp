#include "CrossoverMath.h"

#include <math.h>

// Coefficients for one SVF section at the given corner frequency and Q
static XoverSection computeSection(double freq, double q, double sampleRate) {
  double g = tan(3.14159265358979323846 * freq / sampleRate);
  double k = 1.0 / q;
  double a1 = 1.0 / (1.0 + g * (g + k));
  XoverSection s;
  s.a1 = (float)a1;
  s.a2 = (float)(g * a1);
  s.a3 = (float)(g * g * a1);
  s.k = (float)k;
  return s;
}

XoverBranch xoverComputeBranch(float freq, CrossoverType type, float sampleRate) {
  XoverBranch branch;
  branch.count = 0;
  if (freq <= 0.0f) return branch;

  double f = freq;
  if (f < 10.0) f = 10.0;
  double fMax = 0.45 * sampleRate;
  if (f > fMax) f = fMax;

  const double BUTTERWORTH_Q = 0.70710678118654752;
  switch (type) {
    case CROSSOVER_LR2:
      branch.section[0] = computeSection(f, 0.5, sampleRate);
      branch.count = 1;
      break;
    case CROSSOVER_BW2:
      branch.section[0] = computeSection(f, BUTTERWORTH_Q, sampleRate);
      branch.count = 1;
      break;
    case CROSSOVER_LR4:
      branch.section[0] = computeSection(f, BUTTERWORTH_Q, sampleRate);
      branch.section[1] = branch.section[0];
      branch.count = 2;
      break;
  }
  return branch;
}

// Case-insensitive comparison against a known 3-char token
static bool tokenEquals(const char* s, const char* token) {
  for (int i = 0; i < 3; i++) {
    char a = s[i];
    if (a >= 'a' && a <= 'z') a -= 32;
    if (a != token[i]) return false;
  }
  return s[3] == '\0';
}

bool xoverParseType(const char* s, CrossoverType& out) {
  if (!s) return false;
  if (tokenEquals(s, "LR2")) { out = CROSSOVER_LR2; return true; }
  if (tokenEquals(s, "LR4")) { out = CROSSOVER_LR4; return true; }
  if (tokenEquals(s, "BW2")) { out = CROSSOVER_BW2; return true; }
  return false;
}
