#ifndef DYNAMIC_EQ_MATH_H
#define DYNAMIC_EQ_MATH_H

// Pure math for the dynamic input EQ - the preference curve that tracks the
// level actually playing. Shared by the sketch and the host-native test
// suite; no Arduino/Audio dependencies.
//
// The curve has two anchors. Band frequencies and Qs are shared; only the
// gains differ. At and below the reference level the reference gains apply;
// between reference and loud the gains interpolate linearly in volume-dB,
// and past the loud anchor they hold. Below the reference an automatic
// loudness compensation takes over - no user configuration at all.

#include "PEQMath.h"

// Level floor. Everything quieter than this is treated as -60dB: the volume
// law is a cube of a 0..1 slider, so the bottom of the slider runs off to
// -infinity and the compensation would with it.
const float VOLUME_FLOOR_DB = -60.0f;

// Loudness compensation: TWO HIGH SHELVES WITH NEGATIVE GAIN, not a bass
// boost. Boosting the bass by X dB is the same curve as cutting everything
// above the corner by X dB plus a broadband +X. The cut form keeps the bass
// at unity, so the compensation can never clip and the headroom pad stays
// untouched by it; the boost form would have to charge the pad X dB, which
// costs the same level - just less obviously.
//
// The user-visible consequence is real either way: below the reference
// level the mids fall LOUDNESS_SLOPE_DB_PER_DB faster per dB than the
// slider law suggests. That is inherent to any loudness control, not an
// artefact of the cut form.
//
// Two shelves rather than one because the ISO 226 equal-loudness contours
// steepen twice going down in frequency. The constants are fitted to
// ISO 226:2003 (see test_dynamic_eq_math) to about 1.3dB from 20Hz to 2kHz;
// the fit is deliberately LF-only, so the small 2-6kHz dip and the >10kHz
// lift in the contours are not reproduced.
const float LOUDNESS_SHELF1_HZ = 300.0f;
const float LOUDNESS_SHELF2_HZ = 60.0f;
const float LOUDNESS_SLOPE_DB_PER_DB = 0.25f;
const float LOUDNESS_MAX_DB = 15.0f; // per shelf

// Playback level in dB from a linear amplitude (state.targetVolume), and
// from a volume-slider percent. These differ by design: the sketch cubes
// the 0..1 slider value into state.volume, so a percent is worth three
// times its usual dB - 100% = 0dB, 50% = -18dB, 25% = -36dB. Both floor at
// VOLUME_FLOOR_DB.
float volumeLinearToDb(float linear);
float volumePctToDb(int pct);

// Resolve the effective band gains for the level playing now. 'ref' supplies
// the reference-anchor bands (frequency/Q/enabled come from there too),
// 'loudGain' the loud-anchor gain of each band. With no loud anchor, or at
// or below the reference level, the reference gains pass through unchanged.
void dynamicEqGains(const PEQBand* ref, const float* loudGain, int n,
                    float refDb, float loudDb, bool hasLoud, float volDb,
                    float* outGain);

// How far below the reference level the playback level sits (0 at or above).
float loudnessDropDb(float refDb, float volDb);

// Gain of each compensation shelf for that drop - negative, see above.
float loudnessShelfGainDb(float dropDb);

// Total compensation in dB at 'freq': the summed magnitude of both shelves.
float loudnessCompensationDb(float freq, float dropDb, float sampleRate);

#endif // DYNAMIC_EQ_MATH_H
