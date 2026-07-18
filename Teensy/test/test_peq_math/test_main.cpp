// PEQ coefficient tests: the Cytomic/Simper SVF bell coefficients computed
// by peqComputeBellSvf must realize exactly the RBJ audio-EQ-cookbook
// peaking filter. Verified two ways:
//  1. the intermediate quantities (A, g, k and the SVF coefficients) match
//     an independent double-precision recomputation, and
//  2. the full frequency response of the SVF difference equations matches
//     the RBJ biquad's response across the audio band.
// Plus the edge clamps (20Hz-20kHz, +/-15dB, Q 0.1-10).

#include <unity.h>

#include <cmath>
#include <complex>

#include "PEQMath.h"

static const double kPi = 3.14159265358979323846;
static const double kFs = 44100.0;

// RBJ audio-EQ-cookbook peaking filter, double precision
struct Biquad {
    double b0, b1, b2, a0, a1, a2;
};

static Biquad rbjPeaking(double f0, double gainDb, double Q, double fs) {
    double A = pow(10.0, gainDb / 40.0);
    double w0 = 2.0 * kPi * f0 / fs;
    double alpha = sin(w0) / (2.0 * Q);
    Biquad b;
    b.b0 = 1.0 + alpha * A;
    b.b1 = -2.0 * cos(w0);
    b.b2 = 1.0 - alpha * A;
    b.a0 = 1.0 + alpha / A;
    b.a1 = -2.0 * cos(w0);
    b.a2 = 1.0 - alpha / A;
    return b;
}

static double biquadMagnitudeDb(const Biquad& bq, double freq, double fs) {
    std::complex<double> z = std::polar(1.0, -2.0 * kPi * freq / fs); // z^-1
    std::complex<double> num = bq.b0 + bq.b1 * z + bq.b2 * z * z;
    std::complex<double> den = bq.a0 + bq.a1 * z + bq.a2 * z * z;
    return 20.0 * log10(std::abs(num / den));
}

// Frequency response of the SVF difference equations PEQProcessor runs
// (see PEQProcessor::processBand), derived via its state-space form:
//   v1 = a2*v0 + a1*ic1 - a2*ic2
//   v2 = a3*v0 + a2*ic1 + (1-a3)*ic2
//   ic1' = 2*v1 - ic1, ic2' = 2*v2 - ic2, y = v0 + m1*v1
static double svfMagnitudeDb(const PeqSvfCoeffs& c, double freq, double fs) {
    double a1 = c.a1, a2 = c.a2, a3 = c.a3, m1 = c.m1;

    // x' = F x + G u ; y = H x + D u
    double F[2][2] = {{2.0 * a1 - 1.0, -2.0 * a2},
                      {2.0 * a2, 1.0 - 2.0 * a3}};
    double G[2] = {2.0 * a2, 2.0 * a3};
    double H[2] = {m1 * a1, -m1 * a2};
    double D = 1.0 + m1 * a2;

    std::complex<double> z = std::polar(1.0, 2.0 * kPi * freq / fs);
    // (zI - F)^-1
    std::complex<double> m00 = z - F[0][0], m01 = -F[0][1];
    std::complex<double> m10 = -F[1][0], m11 = z - F[1][1];
    std::complex<double> det = m00 * m11 - m01 * m10;
    std::complex<double> x0 = (m11 * G[0] - m01 * G[1]) / det;
    std::complex<double> x1 = (-m10 * G[0] + m00 * G[1]) / det;
    std::complex<double> Hz = H[0] * x0 + H[1] * x1 + D;
    return 20.0 * log10(std::abs(Hz));
}

// 1. Coefficient identity: the float32 coefficients must equal the
// double-precision RBJ-derived SVF quantities (A = 10^(G/40),
// g = tan(pi*f0/fs), k = 1/(Q*A)) narrowed to float.
static void assertCoefficientIdentity(float f0, float gainDb, float Q) {
    PeqSvfCoeffs c = peqComputeBellSvf(f0, gainDb, Q, (float)kFs);

    double A = pow(10.0, (double)gainDb / 40.0);
    double g = tan(kPi * (double)f0 / kFs);
    double k = 1.0 / ((double)Q * A);
    double a1 = 1.0 / (1.0 + g * (g + k));

    char msg[96];
    snprintf(msg, sizeof(msg), "f0=%g gain=%g Q=%g", f0, gainDb, Q);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE((float)a1, c.a1, msg);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE((float)(g * a1), c.a2, msg);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE((float)(g * g * a1), c.a3, msg);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE((float)(k * (A * A - 1.0)), c.m1, msg);
}

// 2. Response equivalence: the SVF realizes the RBJ peaking response
static void assertResponseMatchesRbj(float f0, float gainDb, float Q) {
    PeqSvfCoeffs c = peqComputeBellSvf(f0, gainDb, Q, (float)kFs);
    Biquad bq = rbjPeaking(f0, gainDb, Q, kFs);

    // Log-spaced probe frequencies across the audio band
    for (int i = 0; i <= 24; i++) {
        double freq = 15.0 * pow(21000.0 / 15.0, i / 24.0);
        double dbSvf = svfMagnitudeDb(c, freq, kFs);
        double dbRbj = biquadMagnitudeDb(bq, freq, kFs);
        char msg[128];
        snprintf(msg, sizeof(msg), "f0=%g gain=%g Q=%g probe=%.1fHz svf=%.4f rbj=%.4f",
                 f0, gainDb, Q, freq, dbSvf, dbRbj);
        // 0.01dB budget covers the float32 narrowing of the coefficients
        TEST_ASSERT_TRUE_MESSAGE(std::fabs(dbSvf - dbRbj) <= 0.01, msg);
    }
}

static const float kFreqs[] = {20.0f, 62.5f, 250.0f, 1000.0f, 4000.0f, 12000.0f, 20000.0f};
static const float kGains[] = {-15.0f, -9.0f, -3.0f, 1.5f, 6.0f, 15.0f};
static const float kQs[] = {0.1f, 0.5f, 0.707f, 1.0f, 4.0f, 10.0f};

static void test_coefficients_match_rbj_derivation(void) {
    for (float f : kFreqs)
        for (float g : kGains)
            for (float q : kQs)
                assertCoefficientIdentity(f, g, q);
}

static void test_response_matches_rbj_cookbook(void) {
    for (float f : kFreqs)
        for (float g : kGains)
            for (float q : kQs)
                assertResponseMatchesRbj(f, g, q);
}

// Edge clamps: out-of-range parameters produce exactly the coefficients of
// the clamped parameters (20Hz-20kHz, +/-15dB, Q 0.1-10)
static void assertSameCoeffs(PeqSvfCoeffs a, PeqSvfCoeffs b, const char* msg) {
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(b.a1, a.a1, msg);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(b.a2, a.a2, msg);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(b.a3, a.a3, msg);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(b.m1, a.m1, msg);
}

static void test_edge_clamps(void) {
    float fs = (float)kFs;
    assertSameCoeffs(peqComputeBellSvf(5.0f, 6.0f, 1.0f, fs),
                     peqComputeBellSvf(20.0f, 6.0f, 1.0f, fs), "freq low clamp");
    assertSameCoeffs(peqComputeBellSvf(30000.0f, 6.0f, 1.0f, fs),
                     peqComputeBellSvf(20000.0f, 6.0f, 1.0f, fs), "freq high clamp");
    assertSameCoeffs(peqComputeBellSvf(1000.0f, 40.0f, 1.0f, fs),
                     peqComputeBellSvf(1000.0f, 15.0f, 1.0f, fs), "gain high clamp");
    assertSameCoeffs(peqComputeBellSvf(1000.0f, -40.0f, 1.0f, fs),
                     peqComputeBellSvf(1000.0f, -15.0f, 1.0f, fs), "gain low clamp");
    assertSameCoeffs(peqComputeBellSvf(1000.0f, 6.0f, 0.01f, fs),
                     peqComputeBellSvf(1000.0f, 6.0f, 0.1f, fs), "Q low clamp");
    assertSameCoeffs(peqComputeBellSvf(1000.0f, 6.0f, 50.0f, fs),
                     peqComputeBellSvf(1000.0f, 6.0f, 10.0f, fs), "Q high clamp");
}

// At lower sample rates the frequency ceiling is 0.49*fs, not 20kHz
static void test_freq_clamp_tracks_sample_rate(void) {
    assertSameCoeffs(peqComputeBellSvf(20000.0f, 6.0f, 1.0f, 22050.0f),
                     peqComputeBellSvf(22050.0f * 0.49f, 6.0f, 1.0f, 22050.0f),
                     "freq clamp at 0.49*fs");
}

// Sanity anchors: at the center frequency the bell peaks at its design gain
// (the bilinear transform is exact at the warped center), and far away it
// returns to ~0dB.
static void test_bell_shape_anchors(void) {
    PeqSvfCoeffs c = peqComputeBellSvf(1000.0f, 9.0f, 1.0f, (float)kFs);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 9.0, svfMagnitudeDb(c, 1000.0, kFs));
    TEST_ASSERT_DOUBLE_WITHIN(0.05, 0.0, svfMagnitudeDb(c, 20.0, kFs));

    // And the shared calculateBellFilter helper agrees at the center
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 9.0f, calculateBellFilter(1000.0f, 1000.0f, 9.0f, 1.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, calculateBellFilter(20000.0f, 20.0f, 9.0f, 4.0f));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_coefficients_match_rbj_derivation);
    RUN_TEST(test_response_matches_rbj_cookbook);
    RUN_TEST(test_edge_clamps);
    RUN_TEST(test_freq_clamp_tracks_sample_rate);
    RUN_TEST(test_bell_shape_anchors);
    return UNITY_END();
}
