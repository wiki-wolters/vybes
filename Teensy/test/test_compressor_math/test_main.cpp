// CompressorMath tests: the static curve (hard slope + quadratic soft
// knee), the voice-activity ramp, and the target computation that folds in
// strength, bypass, makeup and the voice-priority bass duck.

#include <unity.h>

#include <cmath>

#include "CompressorMath.h"

// --- Static curve ---

static void test_curve_below_knee_is_zero(void) {
    // Well under the threshold: no reduction
    TEST_ASSERT_EQUAL_FLOAT(0.0f, compCurveGrDb(-60.0f, -24.0f, 4.0f));
    // Exactly at the lower knee edge
    TEST_ASSERT_EQUAL_FLOAT(0.0f, compCurveGrDb(-24.0f - COMP_KNEE_DB / 2, -24.0f, 4.0f));
}

static void test_curve_above_knee_follows_ratio(void) {
    // 12 dB over a -24 threshold at 4:1 -> keep 1/4 of the overshoot,
    // reduce by 12 * (1 - 1/4) = 9 dB
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 9.0f, compCurveGrDb(-12.0f, -24.0f, 4.0f));
    // 20:1 approaches a limiter: 10 dB over -> 9.5 dB reduction
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 9.5f, compCurveGrDb(-14.0f, -24.0f, 20.0f));
}

static void test_curve_is_continuous_at_knee_edges(void) {
    const float thr = -24.0f, ratio = 4.0f, eps = 1e-3f;
    const float lo = thr - COMP_KNEE_DB / 2, hi = thr + COMP_KNEE_DB / 2;
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, compCurveGrDb(lo - eps, thr, ratio),
                             compCurveGrDb(lo + eps, thr, ratio));
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, compCurveGrDb(hi - eps, thr, ratio),
                             compCurveGrDb(hi + eps, thr, ratio));
    // Inside the knee the reduction is between the two straight lines
    float mid = compCurveGrDb(thr, thr, ratio);
    TEST_ASSERT_TRUE(mid > 0.0f);
    TEST_ASSERT_TRUE(mid < compCurveGrDb(hi, thr, ratio));
}

static void test_curve_ratio_one_is_transparent(void) {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, compCurveGrDb(0.0f, -24.0f, 1.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, compCurveGrDb(0.0f, -24.0f, 0.5f)); // nonsense ratio clamps to off
}

// --- Voice activity ---

static void test_voice_activity_ramp(void) {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, compVoiceActivity(-40.0f, -30.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, compVoiceActivity(-30.0f, -30.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.5f, compVoiceActivity(-30.0f + COMP_KNEE_DB / 2, -30.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, compVoiceActivity(-30.0f + COMP_KNEE_DB, -30.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, compVoiceActivity(0.0f, -30.0f));
}

// --- Target computation ---

static CompParams makeParams(void) {
    CompParams p;
    for (int b = 0; b < COMP_NUM_BANDS; b++) {
        p.band[b].thresholdDb = -24.0f;
        p.band[b].ratio = 4.0f;
        p.band[b].makeupDb = 0.0f;
        p.band[b].bypass = false;
    }
    p.strength = 1.0f;
    p.voicePriorityDb = 0.0f;
    return p;
}

static void test_targets_apply_reduction_as_gain(void) {
    CompParams p = makeParams();
    float env[COMP_NUM_BANDS] = {-12.0f, -60.0f, -60.0f};
    float gain[COMP_NUM_BANDS], gr[COMP_NUM_BANDS];
    compComputeTargets(p, env, 0.0f, gain, gr);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 9.0f, gr[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, powf(10.0f, -9.0f / 20.0f), gain[0]);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, gr[1]);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, gain[1]);
}

static void test_strength_scales_reduction_and_makeup(void) {
    CompParams p = makeParams();
    p.strength = 0.5f;
    p.band[0].makeupDb = 4.0f;
    float env[COMP_NUM_BANDS] = {-12.0f, -60.0f, -60.0f};
    float gain[COMP_NUM_BANDS], gr[COMP_NUM_BANDS];
    compComputeTargets(p, env, 0.0f, gain, gr);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 4.5f, gr[0]); // half of 9
    // gain = makeup*0.5 - gr = 2 - 4.5 = -2.5 dB
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, powf(10.0f, -2.5f / 20.0f), gain[0]);
}

static void test_strength_zero_is_transparent(void) {
    CompParams p = makeParams();
    p.strength = 0.0f;
    p.band[0].makeupDb = 6.0f;
    float env[COMP_NUM_BANDS] = {0.0f, 0.0f, 0.0f};
    float gain[COMP_NUM_BANDS], gr[COMP_NUM_BANDS];
    compComputeTargets(p, env, 12.0f, gain, gr);
    for (int b = 0; b < COMP_NUM_BANDS; b++) {
        TEST_ASSERT_EQUAL_FLOAT(0.0f, gr[b]);
        TEST_ASSERT_EQUAL_FLOAT(1.0f, gain[b]);
    }
}

static void test_bypass_band_is_transparent(void) {
    CompParams p = makeParams();
    p.band[0].bypass = true;
    p.band[0].makeupDb = 6.0f;
    float env[COMP_NUM_BANDS] = {0.0f, -60.0f, -60.0f};
    float gain[COMP_NUM_BANDS], gr[COMP_NUM_BANDS];
    compComputeTargets(p, env, 0.0f, gain, gr);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, gr[0]);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, gain[0]);
}

static void test_voice_duck_lowers_bass_threshold_only(void) {
    CompParams p = makeParams();
    float env[COMP_NUM_BANDS] = {-12.0f, -12.0f, -60.0f};
    float gain[COMP_NUM_BANDS], gr[COMP_NUM_BANDS];
    compComputeTargets(p, env, 6.0f, gain, gr);
    // Bass: threshold effectively -30 -> 18 over -> 13.5 dB reduction
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 13.5f, gr[0]);
    // Mid unaffected by the duck: 9 dB as before
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 9.0f, gr[1]);
}

static void test_smoothing_coeff_sanity(void) {
    // Instant for non-positive times
    TEST_ASSERT_EQUAL_FLOAT(1.0f, compSmoothingCoeff(0.0f, 44100.0f));
    // Longer time constants smooth more (smaller coefficient)
    float fast = compSmoothingCoeff(5.0f, 44100.0f);
    float slow = compSmoothingCoeff(150.0f, 44100.0f);
    TEST_ASSERT_TRUE(fast > slow);
    TEST_ASSERT_TRUE(fast > 0.0f && fast < 1.0f);
    // ~63% of the way there after one time constant's worth of samples
    float coeff = compSmoothingCoeff(10.0f, 44100.0f);
    float g = 0.0f;
    for (int i = 0; i < 441; i++) g += coeff * (1.0f - g);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.632f, g);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_curve_below_knee_is_zero);
    RUN_TEST(test_curve_above_knee_follows_ratio);
    RUN_TEST(test_curve_is_continuous_at_knee_edges);
    RUN_TEST(test_curve_ratio_one_is_transparent);
    RUN_TEST(test_voice_activity_ramp);
    RUN_TEST(test_targets_apply_reduction_as_gain);
    RUN_TEST(test_strength_scales_reduction_and_makeup);
    RUN_TEST(test_strength_zero_is_transparent);
    RUN_TEST(test_bypass_band_is_transparent);
    RUN_TEST(test_voice_duck_lowers_bass_threshold_only);
    RUN_TEST(test_smoothing_coeff_sanity);
    return UNITY_END();
}
