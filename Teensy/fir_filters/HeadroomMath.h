#ifndef HEADROOM_MATH_H
#define HEADROOM_MATH_H

// Pure headroom-pad math, shared by the input EQ compensation and the
// per-output pad on the Teensy plus the host-native test suite - no
// Arduino/Audio dependencies.
//
// The pad exists so EQ boosts can't saturate the float->q15 conversion at
// the PEQ output: attenuate upstream by the maximum positive excursion of
// the summed EQ curve. Two refinements keep it from over-charging (every
// dB of pad rides the signal a real dB closer to the 16-bit inter-stage
// noise floor):
//
//  1. Crossover fold (output EQ only): the output PEQ sits behind the
//     channel's HP/LP crossover, so a boost outside the passband never
//     reaches the output. The sweep sums the crossover's response (never
//     positive - all types run Q <= 0.7071) into the curve. Mathematically
//     safe: this is still the exact worst-case sine gain of the channel.
//
//  2. Spectral allowance: program material carries far less energy at high
//     frequencies (typically 20-40dB below full scale at 10kHz), so a
//     high-frequency boost is charged less than face value. This one is
//     statistical, not guaranteed - a near-full-scale synthetic HF tone
//     through a big HF boost can push into the q15 saturation (a clamp,
//     not a wrap) - so it is deliberately conservative, roughly half of
//     pink weighting: 0dB at and below 2kHz, rising log-linearly to 6dB at
//     20kHz. Below 2kHz real mixes do park near-full-scale energy
//     (vocals, snares, synths), so no allowance is granted there.

#include "PEQMath.h"
#include "CrossoverMath.h"

// Spectral allowance in dB at freq (>= 0; see above)
float headroomAllowanceDb(float freq);

// Maximum positive excursion in dB (>= 0, i.e. the pad to apply) of the
// summed response of the enabled bands plus the two crossover branches,
// less the spectral allowance. Sweeps 20Hz-20kHz logarithmically plus every
// enabled band's center frequency, so narrow high-Q peaks can't slip
// between grid points. hpFreq/lpFreq <= 0 = branch off.
float headroomMaxBoostDb(const PEQBand* bands, int numBands,
                         float hpFreq, CrossoverType hpType,
                         float lpFreq, CrossoverType lpType,
                         float sampleRate);

// Full-range variant for the shared input EQ (no crossover in that path).
// sampleRate is the rate the bells run at: it shapes them toward fs/2.
float headroomMaxBoostDb(const PEQBand* bands, int numBands, float sampleRate);

#endif // HEADROOM_MATH_H
