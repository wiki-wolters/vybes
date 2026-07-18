#ifndef PEQ_MATH_H
#define PEQ_MATH_H

// Pure coefficient math for the parametric EQ, shared by PEQProcessor (on
// the Teensy) and the host-native test suite - no Arduino/Audio
// dependencies.

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

// Exact magnitude response of a bell (peaking) filter in dB at 'freq'.
// This is the same math used by the WebUI to draw the curve, so what you
// see, what is compensated for, and what you hear all agree.
float calculateBellFilter(float freq, float centerFreq, float gain, float q);

#endif // PEQ_MATH_H
