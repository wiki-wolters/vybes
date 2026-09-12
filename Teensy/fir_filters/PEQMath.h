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

// Same SVF, shelf configuration. A shelf mixes all three outputs of the
// filter (input, bandpass, lowpass) where the bell only needs the bandpass,
// so it carries m0 and m2 as well.
struct PeqShelfSvfCoeffs {
  float a1, a2, a3;
  float m0, m1, m2;
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

// High-shelf SVF coefficients at the RBJ cookbook's shelf slope S = 1
// (Q = 1/sqrt(2), no resonance at the corner) - the loudness compensation
// (DynamicEqMath.h) is the only user, and it wants a plain shelf, so the
// slope is fixed rather than a parameter. Same clamps as the bells apply to
// fc and gain.
PeqShelfSvfCoeffs peqComputeHighShelfSvf(float fc, float gainDb,
                                         float sampleRate);

// Exact magnitude response in dB at 'freq' of the high shelf
// peqComputeHighShelfSvf realises - the digital RBJ shelf at 'sampleRate',
// not the analog prototype. Returns the full shelf gain at and above fs/2
// (which is where the digital shelf actually lands).
float highShelfDb(float freq, float fc, float gainDb, float sampleRate);

#endif // PEQ_MATH_H
