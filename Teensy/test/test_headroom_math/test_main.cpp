// Headroom-pad tests: headroomMaxBoostDb must charge exactly the maximum
// net positive excursion of the summed curve - EQ bells, plus the channel's
// crossover response (fold #1: a boost the crossover removes is free), less
// the spectral allowance (fold #2: HF boosts are charged below face value
// because program energy up there sits far under full scale).
//
// Expected values are recomputed independently in double precision over a
// dense grid, the same way the other math suites verify against references.

#include <unity.h>

#include <cmath>

#include "HeadroomMath.h"

static const float FS = 44100.0f;

// --- independent double-precision references ---

// RBJ analog bell magnitude in dB (mirrors calculateBellFilter)
static double refBellDb(double f, double fc, double gainDb, double q) {
    if (gainDb == 0.0) return 0.0;
    double A = pow(10.0, gainDb / 40.0);
    double O = f / fc;
    double c = (1.0 - O * O) * (1.0 - O * O);
    double nb = A * O / q;
    double db = O / (A * q);
    return 10.0 * log10((c + nb * nb) / (c + db * db));
}

static double refAllowanceDb(double f) {
    if (f <= 2000.0) return 0.0;
    if (f >= 20000.0) return 6.0;
    return 6.0 * log10(f / 2000.0);
}

// Analog second-order branch magnitude in dB (LR2: one Q=0.5 section,
// BW2: one Q=0.7071, LR4: two Q=0.7071)
static double refBranchDb(double f, double fc, CrossoverType type, bool hp) {
    if (fc <= 0.0) return 0.0;
    double q = (type == CROSSOVER_LR2) ? 0.5 : 0.70710678118654752;
    int sections = (type == CROSSOVER_LR4) ? 2 : 1;
    double O2 = (f / fc) * (f / fc);
    double c = 1.0 - O2;
    double den = c * c + O2 / (q * q);
    double num = hp ? O2 * O2 : 1.0;
    return sections * 10.0 * log10(num / den);
}

// Dense-grid max of the net curve (4000 points so grid error is negligible)
static double refMaxNetDb(const PEQBand* bands, int numBands,
                          double hpFreq, CrossoverType hpType,
                          double lpFreq, CrossoverType lpType) {
    double maxNet = 0.0;
    const int N = 4000;
    for (int i = 0; i < N; i++) {
        double f = 20.0 * pow(1000.0, (double)i / (N - 1));
        double total = 0.0;
        for (int j = 0; j < numBands; j++) {
            if (bands[j].enabled) {
                total += refBellDb(f, bands[j].frequency, bands[j].gain, bands[j].q);
            }
        }
        total += refBranchDb(f, hpFreq, hpType, true);
        total += refBranchDb(f, lpFreq, lpType, false);
        total -= refAllowanceDb(f);
        if (total > maxNet) maxNet = total;
    }
    return maxNet;
}

static PEQBand band(float freq, float gain, float q) {
    return {freq, gain, q, true};
}

static PEQBand offBand() {
    return {1000.0f, 0.0f, 1.0f, false};
}

// --- the allowance curve itself ---

static void test_allowance_shape(void) {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, headroomAllowanceDb(30.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, headroomAllowanceDb(1000.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, headroomAllowanceDb(2000.0f));
    // 6 * log10(5) at 10kHz
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.19f, headroomAllowanceDb(10000.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.0f, headroomAllowanceDb(20000.0f));
    TEST_ASSERT_EQUAL_FLOAT(6.0f, headroomAllowanceDb(30000.0f));
    // Monotonic through the ramp
    float prev = 0.0f;
    for (float f = 2000.0f; f <= 20000.0f; f *= 1.1f) {
        float a = headroomAllowanceDb(f);
        TEST_ASSERT_TRUE(a >= prev);
        prev = a;
    }
}

// --- full-range (input EQ) behavior ---

static void test_low_boost_pays_full_price(void) {
    PEQBand bands[1] = {band(30.0f, 5.0f, 1.0f)};
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 5.0f, headroomMaxBoostDb(bands, 1));
}

static void test_high_boost_pays_reduced_price(void) {
    PEQBand bands[1] = {band(10000.0f, 5.0f, 1.0f)};
    float pad = headroomMaxBoostDb(bands, 1);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, (float)refMaxNetDb(bands, 1, 0, CROSSOVER_LR4, 0, CROSSOVER_LR4), pad);
    // ~5 - 4.19; well under the 5dB a frequency-blind pad would charge
    TEST_ASSERT_TRUE(pad < 1.0f);
    TEST_ASSERT_TRUE(pad > 0.5f);
}

static void test_same_boost_cheaper_at_hf_than_lf(void) {
    PEQBand lf[1] = {band(30.0f, 5.0f, 1.0f)};
    PEQBand hf[1] = {band(10000.0f, 5.0f, 1.0f)};
    TEST_ASSERT_TRUE(headroomMaxBoostDb(hf, 1) < headroomMaxBoostDb(lf, 1));
}

static void test_cuts_cost_nothing(void) {
    PEQBand bands[2] = {band(100.0f, -6.0f, 1.0f), band(5000.0f, -3.0f, 2.0f)};
    TEST_ASSERT_EQUAL_FLOAT(0.0f, headroomMaxBoostDb(bands, 2));
}

static void test_disabled_bands_ignored(void) {
    PEQBand bands[2] = {band(100.0f, 12.0f, 1.0f), offBand()};
    bands[0].enabled = false;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, headroomMaxBoostDb(bands, 2));
}

static void test_overlapping_boosts_sum(void) {
    PEQBand bands[2] = {band(100.0f, 3.0f, 1.0f), band(100.0f, 3.0f, 1.0f)};
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 6.0f, headroomMaxBoostDb(bands, 2));
}

// A narrow high-Q band centered between grid points must still be charged
// in full - the sweep evaluates every enabled band's center frequency
static void test_high_q_off_grid_center_fully_counted(void) {
    PEQBand bands[1] = {band(9700.0f, 8.0f, 10.0f)};
    float expected = 8.0f - headroomAllowanceDb(9700.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, expected, headroomMaxBoostDb(bands, 1));
}

// --- crossover fold (output EQ) ---

static void test_boost_outside_passband_is_free(void) {
    // +5dB at 30Hz on a tweeter channel high-passed at 2kHz: the crossover
    // removes the boosted region entirely, so no pad is charged
    PEQBand bands[1] = {band(30.0f, 5.0f, 1.0f)};
    float pad = headroomMaxBoostDb(bands, 1, 2000.0f, CROSSOVER_LR4,
                                   0.0f, CROSSOVER_LR4, FS);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.0f, pad);
}

static void test_boost_inside_passband_still_charged(void) {
    // +5dB at 500Hz with an HP far below it: nearly the full face value
    // (500Hz is below the allowance ramp)
    PEQBand bands[1] = {band(500.0f, 5.0f, 1.0f)};
    float pad = headroomMaxBoostDb(bands, 1, 100.0f, CROSSOVER_LR4,
                                   0.0f, CROSSOVER_LR4, FS);
    TEST_ASSERT_FLOAT_WITHIN(0.05f,
        (float)refMaxNetDb(bands, 1, 100.0, CROSSOVER_LR4, 0.0, CROSSOVER_LR4), pad);
    TEST_ASSERT_TRUE(pad > 4.9f);
}

static void test_boost_near_corner_partially_charged(void) {
    // +5dB at 3kHz just above an LR2 HP at 2kHz: charged for what survives
    // the corner, verified against the independent reference
    PEQBand bands[1] = {band(3000.0f, 5.0f, 1.0f)};
    float pad = headroomMaxBoostDb(bands, 1, 2000.0f, CROSSOVER_LR2,
                                   0.0f, CROSSOVER_LR4, FS);
    float expected = (float)refMaxNetDb(bands, 1, 2000.0, CROSSOVER_LR2, 0.0, CROSSOVER_LR4);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, expected, pad);
    TEST_ASSERT_TRUE(pad > 0.0f);
    TEST_ASSERT_TRUE(pad < 5.0f);
}

static void test_crossover_alone_never_creates_pad(void) {
    PEQBand bands[1] = {offBand()};
    float pad = headroomMaxBoostDb(bands, 1, 80.0f, CROSSOVER_LR4,
                                   2500.0f, CROSSOVER_BW2, FS);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pad);
}

static void test_input_variant_matches_branches_off(void) {
    PEQBand bands[2] = {band(60.0f, 4.0f, 2.0f), band(8000.0f, 6.0f, 3.0f)};
    TEST_ASSERT_EQUAL_FLOAT(
        headroomMaxBoostDb(bands, 2, 0.0f, CROSSOVER_LR4, 0.0f, CROSSOVER_LR4, FS),
        headroomMaxBoostDb(bands, 2));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_allowance_shape);
    RUN_TEST(test_low_boost_pays_full_price);
    RUN_TEST(test_high_boost_pays_reduced_price);
    RUN_TEST(test_same_boost_cheaper_at_hf_than_lf);
    RUN_TEST(test_cuts_cost_nothing);
    RUN_TEST(test_disabled_bands_ignored);
    RUN_TEST(test_overlapping_boosts_sum);
    RUN_TEST(test_high_q_off_grid_center_fully_counted);
    RUN_TEST(test_boost_outside_passband_is_free);
    RUN_TEST(test_boost_inside_passband_still_charged);
    RUN_TEST(test_boost_near_corner_partially_charged);
    RUN_TEST(test_crossover_alone_never_creates_pad);
    RUN_TEST(test_input_variant_matches_branches_off);
    return UNITY_END();
}
