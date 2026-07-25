// CrossoverMath tests: the HP/LP SVF sections are verified against the
// textbook responses by measuring the magnitude of their impulse responses
// with a direct DFT - LR2/LR4 are -6dB at fc, BW2 is -3dB, slopes are
// 12/24 dB per octave, and the LR HP+LP branches sum to an allpass.

#include <unity.h>

#include <cmath>
#include <string>
#include <vector>

#include "CrossoverMath.h"

static const float FS = 44100.0f;
static const int IR_LEN = 16384;

// Impulse response of one branch (cascade of its sections)
static std::vector<float> impulseResponse(const XoverBranch& branch, bool highpass) {
    XoverSectionState st[2] = {{0, 0}, {0, 0}};
    std::vector<float> h(IR_LEN);
    for (int i = 0; i < IR_LEN; i++) {
        float x = (i == 0) ? 1.0f : 0.0f;
        for (int s = 0; s < branch.count; s++) {
            x = highpass ? xoverProcessHighpass(branch.section[s], st[s], x)
                         : xoverProcessLowpass(branch.section[s], st[s], x);
        }
        h[i] = x;
    }
    return h;
}

// Magnitude in dB at one frequency, by direct DFT of the impulse response
static float magnitudeDb(const std::vector<float>& h, float freq) {
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < h.size(); i++) {
        double phase = 2.0 * M_PI * freq * (double)i / FS;
        re += h[i] * cos(phase);
        im -= h[i] * sin(phase);
    }
    return 20.0f * (float)log10(sqrt(re * re + im * im) + 1e-30);
}

static float branchMagDb(float fc, CrossoverType type, bool highpass, float atFreq) {
    XoverBranch b = xoverComputeBranch(fc, type, FS);
    return magnitudeDb(impulseResponse(b, highpass), atFreq);
}

// --- corner-frequency levels ---

static void test_lr4_is_minus_6dB_at_fc(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -6.02f, branchMagDb(1000.0f, CROSSOVER_LR4, true, 1000.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -6.02f, branchMagDb(1000.0f, CROSSOVER_LR4, false, 1000.0f));
    // Low sub-crossover frequencies are the primary use - check one there too
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -6.02f, branchMagDb(60.0f, CROSSOVER_LR4, false, 60.0f));
}

static void test_lr2_is_minus_6dB_at_fc(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -6.02f, branchMagDb(1000.0f, CROSSOVER_LR2, true, 1000.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -6.02f, branchMagDb(80.0f, CROSSOVER_LR2, false, 80.0f));
}

static void test_bw2_is_minus_3dB_at_fc(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -3.01f, branchMagDb(1000.0f, CROSSOVER_BW2, true, 1000.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -3.01f, branchMagDb(1000.0f, CROSSOVER_BW2, false, 1000.0f));
}

// --- slopes and passbands ---

static void test_slopes_and_passbands(void) {
    // Passband is flat (well away from fc)
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 0.0f, branchMagDb(1000.0f, CROSSOVER_LR4, true, 8000.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 0.0f, branchMagDb(1000.0f, CROSSOVER_LR4, false, 125.0f));

    // Two octaves into the stopband: LR4 ~ -48dB, LR2/BW2 ~ -24dB
    TEST_ASSERT_TRUE(branchMagDb(1000.0f, CROSSOVER_LR4, true, 250.0f) < -40.0f);
    TEST_ASSERT_TRUE(branchMagDb(1000.0f, CROSSOVER_LR2, true, 250.0f) < -20.0f);
    TEST_ASSERT_TRUE(branchMagDb(1000.0f, CROSSOVER_BW2, true, 250.0f) < -20.0f);
}

// --- LR summation ---

// A Linkwitz-Riley HP + LP pair sums to an allpass: flat magnitude through
// the crossover region (that's the reason LR types exist).
static void test_lr4_hp_plus_lp_sums_flat(void) {
    XoverBranch b = xoverComputeBranch(1000.0f, CROSSOVER_LR4, FS);
    std::vector<float> hp = impulseResponse(b, true);
    std::vector<float> lp = impulseResponse(b, false);
    std::vector<float> sum(IR_LEN);
    for (int i = 0; i < IR_LEN; i++) sum[i] = hp[i] + lp[i];

    const float freqs[] = {100.0f, 500.0f, 1000.0f, 2000.0f, 10000.0f};
    for (float f : freqs) {
        char msg[48];
        snprintf(msg, sizeof(msg), "LR4 sum at %.0f Hz", f);
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.2f, 0.0f, magnitudeDb(sum, f), msg);
    }
}

// --- configuration plumbing ---

static void test_freq_zero_disables_branch(void) {
    XoverBranch b = xoverComputeBranch(0.0f, CROSSOVER_LR4, FS);
    TEST_ASSERT_EQUAL_INT(0, b.count);
    b = xoverComputeBranch(-5.0f, CROSSOVER_LR2, FS);
    TEST_ASSERT_EQUAL_INT(0, b.count);
}

static void test_section_counts(void) {
    TEST_ASSERT_EQUAL_INT(1, xoverComputeBranch(100.0f, CROSSOVER_LR2, FS).count);
    TEST_ASSERT_EQUAL_INT(1, xoverComputeBranch(100.0f, CROSSOVER_BW2, FS).count);
    TEST_ASSERT_EQUAL_INT(2, xoverComputeBranch(100.0f, CROSSOVER_LR4, FS).count);
}

static void test_type_parsing(void) {
    CrossoverType t = CROSSOVER_BW2;
    TEST_ASSERT_TRUE(xoverParseType("LR2", t));
    TEST_ASSERT_EQUAL_INT(CROSSOVER_LR2, t);
    TEST_ASSERT_TRUE(xoverParseType("lr4", t));
    TEST_ASSERT_EQUAL_INT(CROSSOVER_LR4, t);
    TEST_ASSERT_TRUE(xoverParseType("Bw2", t));
    TEST_ASSERT_EQUAL_INT(CROSSOVER_BW2, t);
    TEST_ASSERT_FALSE(xoverParseType("LR8", t));
    TEST_ASSERT_FALSE(xoverParseType("LR24", t));
    TEST_ASSERT_FALSE(xoverParseType("", t));
    TEST_ASSERT_FALSE(xoverParseType(nullptr, t));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_lr4_is_minus_6dB_at_fc);
    RUN_TEST(test_lr2_is_minus_6dB_at_fc);
    RUN_TEST(test_bw2_is_minus_3dB_at_fc);
    RUN_TEST(test_slopes_and_passbands);
    RUN_TEST(test_lr4_hp_plus_lp_sums_flat);
    RUN_TEST(test_freq_zero_disables_branch);
    RUN_TEST(test_section_counts);
    RUN_TEST(test_type_parsing);
    return UNITY_END();
}
