// Dynamic input EQ tests (DynamicEqMath.h): the volume law, the anchor
// interpolation, and - the one that matters - whether the loudness
// compensation actually tracks the ISO 226:2003 equal-loudness contours.
//
// That last test embeds the standard's own table and formula rather than a
// pre-baked expected curve, so the two shelves are checked against the
// physics they claim to model, not against themselves.

#include <unity.h>

#include <cmath>
#include <cstdio>

#include "DynamicEqMath.h"

static const float FS = 44100.0f;

// --- ISO 226:2003 equal-loudness contours ---
//
// The 29 tabulated frequencies with their af / Lu / Tf coefficients, and
// the standard's formula for the sound pressure level Lp that a tone at
// frequency f must reach to be heard at loudness level Ln (phons):
//   Af = 4.47e-3*(10^(0.025*Ln) - 1.15) + (0.4*10^((Tf+Lu)/10 - 9))^af
//   Lp = (10/af)*log10(Af) - Lu + 94
static const int kIsoCount = 29;
static const double kIsoFreq[kIsoCount] = {
    20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500,
    630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000,
    10000, 12500};
static const double kIsoAf[kIsoCount] = {
    0.532, 0.506, 0.480, 0.455, 0.432, 0.409, 0.387, 0.367, 0.349, 0.330,
    0.315, 0.301, 0.288, 0.276, 0.267, 0.259, 0.253, 0.250, 0.246, 0.244,
    0.243, 0.243, 0.243, 0.242, 0.242, 0.245, 0.254, 0.271, 0.301};
static const double kIsoLu[kIsoCount] = {
    -31.6, -27.2, -23.0, -19.1, -15.9, -13.0, -10.3, -8.1, -6.2, -4.5,
    -3.1, -2.0, -1.1, -0.4, 0.0, 0.3, 0.5, 0.0, -2.7, -4.1,
    -1.0, 1.7, 2.5, 1.2, -2.1, -7.1, -11.2, -10.7, -3.1};
static const double kIsoTf[kIsoCount] = {
    78.5, 68.7, 59.5, 51.1, 44.0, 37.5, 31.5, 26.5, 22.1, 17.9,
    14.4, 11.4, 8.6, 6.2, 4.4, 3.0, 2.2, 2.4, 3.5, 1.7,
    -1.3, -4.2, -6.0, -5.4, -1.5, 6.0, 12.6, 13.9, 12.3};

static double isoSplForPhon(int index, double phon) {
    double af = kIsoAf[index], Lu = kIsoLu[index], Tf = kIsoTf[index];
    double Af = 4.47e-3 * (pow(10.0, 0.025 * phon) - 1.15) +
                pow(0.4 * pow(10.0, (Tf + Lu) / 10.0 - 9.0), af);
    return (10.0 / af) * log10(Af) - Lu + 94.0;
}

// The table and formula are transcribed right: at 1kHz the phon scale is
// defined to equal the SPL scale.
static void test_iso226_reference_point(void) {
    int i1k = -1;
    for (int i = 0; i < kIsoCount; i++) {
        if (kIsoFreq[i] == 1000.0) i1k = i;
    }
    TEST_ASSERT_TRUE(i1k >= 0);
    TEST_ASSERT_DOUBLE_WITHIN(0.05, 40.0, isoSplForPhon(i1k, 40.0));
    TEST_ASSERT_DOUBLE_WITHIN(0.05, 80.0, isoSplForPhon(i1k, 80.0));
}

// THE physics test. Turning the volume down by D dB does not just make
// everything quieter: relative to 1kHz, the low end loses more, and the
// contours say exactly how much. For a reference level of 80 phon, the
// correction the compensation owes at frequency f after a drop of D is
//   Lp(f, 80-D) - Lp(f, 80) + D
// (zero at 1kHz by construction). The two shelves have to reproduce that,
// relative to their own 1kHz value, across the low end.
//
// 1.5dB, over 20Hz-2kHz: the shelves are a deliberately LF-only fit, so the
// small 2-6kHz dip and the >10kHz lift in the contours are not modelled -
// and a fraction of a dB of tilt down there is inaudible next to what the
// compensation is correcting. Measured worst case is about 1.25dB.
static void test_loudness_tracks_iso226_contours(void) {
    const double reference = 80.0;

    for (double drop = 5.0; drop <= 40.0; drop += 5.0) {
        double at1k = loudnessCompensationDb(1000.0f, (float)drop, FS);

        for (int i = 0; i < kIsoCount; i++) {
            double f = kIsoFreq[i];
            if (f < 20.0 || f > 2000.0) continue;

            double got = loudnessCompensationDb((float)f, (float)drop, FS) - at1k;
            double want = isoSplForPhon(i, reference - drop) -
                          isoSplForPhon(i, reference) + drop;

            char msg[96];
            snprintf(msg, sizeof(msg), "drop=%gdB f=%gHz", drop, f);
            TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(1.5, want, got, msg);
        }
    }
}

// The compensation cuts, it never boosts: the bass stays at unity so it
// cannot clip, and the headroom pad never has to charge for it.
static void test_loudness_only_ever_cuts(void) {
    for (double drop = 0.0; drop <= 80.0; drop += 2.5) {
        for (double f = 20.0; f <= 20000.0; f *= 1.2) {
            float db = loudnessCompensationDb((float)f, (float)drop, FS);
            char msg[96];
            snprintf(msg, sizeof(msg), "drop=%gdB f=%gHz -> %gdB", drop, f, (double)db);
            TEST_ASSERT_TRUE_MESSAGE(db <= 0.0001f, msg);
        }
    }
    // ...and at the reference level there is no compensation at all
    TEST_ASSERT_EQUAL_FLOAT(0.0f, loudnessCompensationDb(20.0f, 0.0f, FS));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, loudnessCompensationDb(20000.0f, 0.0f, FS));
}

static void test_loudness_shelf_gain_slope_and_cap(void) {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, loudnessShelfGainDb(0.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, loudnessShelfGainDb(-10.0f)); // above reference
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -3.0f, loudnessShelfGainDb(12.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -LOUDNESS_MAX_DB, loudnessShelfGainDb(60.0f));
    // The cap holds however far down the slider goes
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -LOUDNESS_MAX_DB, loudnessShelfGainDb(200.0f));
}

static void test_loudness_drop_is_one_sided(void) {
    TEST_ASSERT_EQUAL_FLOAT(12.0f, loudnessDropDb(-18.0f, -30.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, loudnessDropDb(-18.0f, -18.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, loudnessDropDb(-18.0f, -6.0f)); // louder
}

// --- volume law ---

// The sketch cubes the 0..1 slider value, so a percent is worth 60*log10,
// not 20: 100% = 0dB, 79% = -6dB, 63% = -12dB, 50% = -18dB, 25% = -36dB.
static void test_volume_pct_to_db(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, volumePctToDb(100));
    TEST_ASSERT_FLOAT_WITHIN(0.15f, -6.0f, volumePctToDb(79));
    TEST_ASSERT_FLOAT_WITHIN(0.15f, -12.0f, volumePctToDb(63));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -18.0f, volumePctToDb(50));
    TEST_ASSERT_FLOAT_WITHIN(0.2f, -36.0f, volumePctToDb(25));
    // The bottom of the slider runs to -infinity; the floor catches it
    TEST_ASSERT_EQUAL_FLOAT(VOLUME_FLOOR_DB, volumePctToDb(0));
    TEST_ASSERT_EQUAL_FLOAT(VOLUME_FLOOR_DB, volumePctToDb(-5));
    TEST_ASSERT_EQUAL_FLOAT(VOLUME_FLOOR_DB, volumePctToDb(1));
}

// The linear form reads state.targetVolume, which is already cubed - so the
// two agree when fed the same slider position.
static void test_volume_linear_to_db(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, volumeLinearToDb(1.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -6.0206f, volumeLinearToDb(0.5f));
    TEST_ASSERT_EQUAL_FLOAT(VOLUME_FLOOR_DB, volumeLinearToDb(0.0f));
    TEST_ASSERT_EQUAL_FLOAT(VOLUME_FLOOR_DB, volumeLinearToDb(-1.0f));
    TEST_ASSERT_EQUAL_FLOAT(VOLUME_FLOOR_DB, volumeLinearToDb(1e-9f));

    for (int pct = 5; pct <= 100; pct += 5) {
        float linear = powf((float)pct / 100.0f, 3.0f);
        TEST_ASSERT_FLOAT_WITHIN(0.01f, volumePctToDb(pct), volumeLinearToDb(linear));
    }
}

// --- anchor interpolation ---

static void fillBands(PEQBand* bands, float* loud, int n) {
    for (int i = 0; i < n; i++) {
        bands[i].frequency = 100.0f * (float)(i + 1);
        bands[i].q = 1.0f;
        bands[i].enabled = true;
        bands[i].gain = (float)i;        // reference gains 0, 1, 2...
        loud[i] = -(float)i;             // loud gains 0, -1, -2...
    }
}

static void test_gains_hold_reference_below_and_at_reference(void) {
    PEQBand ref[4];
    float loud[4], out[4];
    fillBands(ref, loud, 4);

    // Below the reference the curve does not move (the shelves do that job)
    dynamicEqGains(ref, loud, 4, -18.0f, -6.0f, true, -40.0f, out);
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_FLOAT(ref[i].gain, out[i]);

    // ...and at the reference exactly
    dynamicEqGains(ref, loud, 4, -18.0f, -6.0f, true, -18.0f, out);
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_FLOAT(ref[i].gain, out[i]);
}

static void test_gains_interpolate_and_hold_above_loud(void) {
    PEQBand ref[4];
    float loud[4], out[4];
    fillBands(ref, loud, 4);

    // Halfway between -18 and -6 in volume-dB is halfway between the anchors
    dynamicEqGains(ref, loud, 4, -18.0f, -6.0f, true, -12.0f, out);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f * (ref[i].gain + loud[i]), out[i]);
    }

    // At the loud anchor, and past it, the loud gains hold
    dynamicEqGains(ref, loud, 4, -18.0f, -6.0f, true, -6.0f, out);
    for (int i = 0; i < 4; i++) TEST_ASSERT_FLOAT_WITHIN(0.001f, loud[i], out[i]);

    dynamicEqGains(ref, loud, 4, -18.0f, -6.0f, true, 0.0f, out);
    for (int i = 0; i < 4; i++) TEST_ASSERT_FLOAT_WITHIN(0.001f, loud[i], out[i]);
}

// Without a loud anchor the reference curve holds at every level, however
// loud - and a malformed anchor pair (loud at or below reference) must not
// divide by zero or run the interpolation backwards.
static void test_gains_without_loud_anchor(void) {
    PEQBand ref[4];
    float loud[4], out[4];
    fillBands(ref, loud, 4);

    dynamicEqGains(ref, loud, 4, -18.0f, VOLUME_FLOOR_DB, false, 0.0f, out);
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_FLOAT(ref[i].gain, out[i]);

    dynamicEqGains(ref, loud, 4, -18.0f, -18.0f, true, 0.0f, out);
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_FLOAT(ref[i].gain, out[i]);

    dynamicEqGains(ref, loud, 4, -18.0f, -30.0f, true, 0.0f, out);
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_FLOAT(ref[i].gain, out[i]);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_iso226_reference_point);
    RUN_TEST(test_loudness_tracks_iso226_contours);
    RUN_TEST(test_loudness_only_ever_cuts);
    RUN_TEST(test_loudness_shelf_gain_slope_and_cap);
    RUN_TEST(test_loudness_drop_is_one_sided);
    RUN_TEST(test_volume_pct_to_db);
    RUN_TEST(test_volume_linear_to_db);
    RUN_TEST(test_gains_hold_reference_below_and_at_reference);
    RUN_TEST(test_gains_interpolate_and_hold_above_loud);
    RUN_TEST(test_gains_without_loud_anchor);
    return UNITY_END();
}
