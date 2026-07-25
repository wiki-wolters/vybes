#ifndef CROSSOVER_MATH_H
#define CROSSOVER_MATH_H

// Pure filter math for the per-output HP/LP crossover branches, shared by
// CrossoverFilter (on the Teensy) and the host-native test suite - no
// Arduino/Audio dependencies.
//
// Each branch is a cascade of up to two Cytomic/Simper trapezoidal SVF
// sections (the same topology PEQProcessor uses, chosen for its float32
// stability at low frequencies - sub crossovers live at 30-120Hz):
//   LR2 = one section, Q 0.5      (12 dB/oct, -6 dB at fc)
//   BW2 = one section, Q 0.7071   (12 dB/oct, -3 dB at fc)
//   LR4 = two sections, Q 0.7071  (24 dB/oct, -6 dB at fc)

// Values match the wire protocol strings LR2/LR4/BW2 (see xoverParseType)
enum CrossoverType {
  CROSSOVER_LR2,
  CROSSOVER_LR4,
  CROSSOVER_BW2,
};

// One SVF section's coefficients
struct XoverSection {
  float a1, a2, a3; // integrator coefficients
  float k;          // damping (1/Q), used by the highpass output mix
};

// A configured HP or LP branch: 0 sections = branch off (passthrough)
struct XoverBranch {
  XoverSection section[2];
  int count;
};

// Per-section filter state (integrators)
struct XoverSectionState {
  float ic1eq, ic2eq;
};

// Build the sections for one branch. freq <= 0 turns the branch off
// (count 0). The frequency is clamped to [10Hz, 0.45 * sampleRate]; the
// coefficient math runs in double precision and narrows to float32.
XoverBranch xoverComputeBranch(float freq, CrossoverType type, float sampleRate);

// Parse a wire-protocol type token ("LR2", "LR4", "BW2", case-insensitive).
// Returns false (leaving out untouched) for anything else.
bool xoverParseType(const char* s, CrossoverType& out);

// Process one sample through one section. The two functions share the SVF
// core and differ only in which output is taken.
inline float xoverProcessHighpass(const XoverSection& c, XoverSectionState& st, float v0) {
  float v3 = v0 - st.ic2eq;
  float v1 = c.a1 * st.ic1eq + c.a2 * v3;
  float v2 = st.ic2eq + c.a2 * st.ic1eq + c.a3 * v3;
  st.ic1eq = 2.0f * v1 - st.ic1eq;
  st.ic2eq = 2.0f * v2 - st.ic2eq;
  return v0 - c.k * v1 - v2;
}

inline float xoverProcessLowpass(const XoverSection& c, XoverSectionState& st, float v0) {
  float v3 = v0 - st.ic2eq;
  float v1 = c.a1 * st.ic1eq + c.a2 * v3;
  float v2 = st.ic2eq + c.a2 * st.ic1eq + c.a3 * v3;
  st.ic1eq = 2.0f * v1 - st.ic1eq;
  st.ic2eq = 2.0f * v2 - st.ic2eq;
  return v2;
}

#endif // CROSSOVER_MATH_H
