#ifndef PEQ_MATH_H
#define PEQ_MATH_H

// Pure coefficient math for the parametric EQ, shared by PEQProcessor (on
// the Teensy) and the host-native test suite - no Arduino/Audio
// dependencies.

// One parametric EQ band. Defined here rather than in PEQProcessor.h so the
// pure headroom math (HeadroomMath.h) can consume band arrays host-side.
struct PEQBand {
  float frequency;
  float gain;
  float q;
  bool enabled;
};

// Cytomic/Simper trapezoidal SVF bell coefficients (see PEQProcessor.cpp for
// the filter loop that consumes them).
struct PeqSvfCoeffs {
  float a1, a2, a3; // integrator coefficients
  float m1;         // bell mix coefficient
};

// Compute the bell SVF coefficients for one band. Inputs are clamped to the
// ranges PEQProcessor enforces: frequency to [20Hz, min(20kHz, 0.49*fs)],
// gain to +/-15dB, Q to [0.1, 10]. The math is done in double precision;
// only the resulting coefficients are narrowed to float32.
PeqSvfCoeffs peqComputeBellSvf(float frequency, float gain, float q, float sampleRate);

// Exact magnitude response in dB at 'freq' of the bell peqComputeBellSvf
// realises for these parameters (same clamps applied): the RBJ peaking
// biquad at 'sampleRate', not the analog prototype, which drifts from the
// running filter above a few kHz. The WebUI draws this same digital curve
// (eq-math.js peakingBellDb), so what you see, what is compensated for,
// and what you hear all agree. Returns 0 at and above fs/2.
float calculateBellFilter(float freq, float centerFreq, float gain, float q,
                          float sampleRate);

#endif // PEQ_MATH_H
