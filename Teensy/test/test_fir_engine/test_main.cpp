// FIR engine equivalence tests: both the direct CMSIS path and the
// uniformly partitioned overlap-save fast-convolution path must produce the
// same output as an independent naive convolution computed in double
// precision, block by block, for tap counts spanning partition boundaries.

#include <unity.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "FirEngine.h"

static const int BLOCK = FirEngine::BLOCK_SAMPLES;

// --- Deterministic PRNG (xorshift32), so failures are reproducible ---
static uint32_t rngState = 1;
static void rngSeed(uint32_t s) { rngState = s ? s : 1; }
static float rngFloat() { // uniform in [-1, 1)
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return (float)((rngState >> 8) / 8388607.5 - 1.0);
}

static std::vector<float> randomVector(size_t n, uint32_t seed) {
  rngSeed(seed);
  std::vector<float> v(n);
  for (size_t i = 0; i < n; i++) v[i] = rngFloat();
  return v;
}

// Independent reference: plain O(N*M) convolution in double precision.
// y[n] = sum_k h[k] * x[n-k], with x[<0] = 0.
static std::vector<double> referenceConvolution(const std::vector<float>& h,
                                                const std::vector<float>& x) {
  std::vector<double> y(x.size(), 0.0);
  for (size_t n = 0; n < x.size(); n++) {
    double acc = 0.0;
    size_t kMax = (n + 1 < h.size()) ? n + 1 : h.size();
    for (size_t k = 0; k < kMax; k++) {
      acc += (double)h[k] * (double)x[n - k];
    }
    y[n] = acc;
  }
  return y;
}

// Push x through the engine block by block.
static std::vector<float> processStream(FirEngine& engine, const std::vector<float>& x) {
  std::vector<float> y(x.size());
  for (size_t off = 0; off < x.size(); off += BLOCK) {
    engine.processBlock(x.data() + off, y.data() + off);
  }
  return y;
}

static double maxAbs(const std::vector<double>& v) {
  double m = 0.0;
  for (double s : v) if (std::fabs(s) > m) m = std::fabs(s);
  return m;
}

// Compare an engine's output against the double-precision reference.
// Tolerance scales with the reference signal's peak and with sqrt(taps)
// (the error growth of a length-N float32 accumulation / FFT).
static void assertMatchesReference(const std::vector<float>& got,
                                   const std::vector<double>& ref,
                                   uint16_t taps, const char* label) {
  double scale = maxAbs(ref);
  if (scale < 1.0) scale = 1.0;
  double tol = 1e-6 * std::sqrt((double)taps) * scale;
  if (tol < 1e-6) tol = 1e-6;

  double maxErr = 0.0;
  size_t worst = 0;
  for (size_t i = 0; i < ref.size(); i++) {
    double err = std::fabs((double)got[i] - ref[i]);
    if (err > maxErr) { maxErr = err; worst = i; }
  }
  char msg[160];
  snprintf(msg, sizeof(msg), "%s taps=%u maxErr=%.3g tol=%.3g at sample %zu",
           label, (unsigned)taps, maxErr, tol, worst);
  TEST_ASSERT_TRUE_MESSAGE(maxErr <= tol, msg);
}

// Tap counts deliberately spanning the 128-sample partition boundaries
static const uint16_t kTapCounts[] = {1, 100, 128, 129, 500, 4096};

static void runEquivalence(bool fast) {
  for (uint16_t taps : kTapCounts) {
    // Enough blocks for the longest filter to fully engage, plus tail
    size_t blocks = (size_t)(taps / BLOCK) + 6;
    std::vector<float> h = randomVector(taps, 0xC0FFEE00u + taps);
    std::vector<float> x = randomVector(blocks * BLOCK, 0xBEEF0000u + taps);

    FirEngine engine;
    engine.setFastConvolution(fast);
    TEST_ASSERT_TRUE(engine.loadCoefficients(h.data(), taps));
    TEST_ASSERT_EQUAL_UINT16(taps, engine.taps());
    TEST_ASSERT_EQUAL(fast, engine.fastLoaded());

    std::vector<float> y = processStream(engine, x);
    std::vector<double> ref = referenceConvolution(h, x);
    assertMatchesReference(y, ref, taps, fast ? "fast" : "direct");
  }
}

static void test_direct_matches_reference(void) { runEquivalence(false); }
static void test_fast_matches_reference(void) { runEquivalence(true); }

// Reloading coefficients mid-stream must keep producing valid output: the
// engine restarts from silent history, so post-reload output is the
// convolution of the new filter with the post-reload input only.
static void runReloadMidStream(bool fast) {
  const uint16_t tapsA = 500, tapsB = 200;
  const size_t blocksEach = 8;
  std::vector<float> hA = randomVector(tapsA, 0xAAAA0001u);
  std::vector<float> hB = randomVector(tapsB, 0xBBBB0002u);
  std::vector<float> x1 = randomVector(blocksEach * BLOCK, 0x11110003u);
  std::vector<float> x2 = randomVector(blocksEach * BLOCK, 0x22220004u);

  FirEngine engine;
  engine.setFastConvolution(fast);
  TEST_ASSERT_TRUE(engine.loadCoefficients(hA.data(), tapsA));
  std::vector<float> y1 = processStream(engine, x1);
  assertMatchesReference(y1, referenceConvolution(hA, x1), tapsA,
                         fast ? "fast pre-reload" : "direct pre-reload");

  // Swap filters mid-stream
  TEST_ASSERT_TRUE(engine.loadCoefficients(hB.data(), tapsB));
  TEST_ASSERT_EQUAL_UINT16(tapsB, engine.taps());
  std::vector<float> y2 = processStream(engine, x2);
  for (float s : y2) TEST_ASSERT_TRUE_MESSAGE(std::isfinite(s), "non-finite output after reload");
  assertMatchesReference(y2, referenceConvolution(hB, x2), tapsB,
                         fast ? "fast post-reload" : "direct post-reload");
}

static void test_direct_reload_mid_stream(void) { runReloadMidStream(false); }
static void test_fast_reload_mid_stream(void) { runReloadMidStream(true); }

// Zero taps = passthrough, matching the AudioStream wrapper's bypass
static void test_zero_taps_passthrough(void) {
  std::vector<float> x = randomVector(BLOCK, 0x51EE7u);
  for (bool fast : {false, true}) {
    FirEngine engine;
    engine.setFastConvolution(fast);
    TEST_ASSERT_TRUE(engine.loadCoefficients(nullptr, 0));
    TEST_ASSERT_EQUAL_UINT16(0, engine.taps());
    float out[BLOCK];
    engine.processBlock(x.data(), out);
    TEST_ASSERT_EQUAL_MEMORY(x.data(), out, sizeof(out));
  }
}

// Clearing a loaded filter (load nullptr/0) returns to passthrough
static void test_clear_filter_returns_to_passthrough(void) {
  std::vector<float> h = randomVector(64, 0xD00Du);
  std::vector<float> x = randomVector(BLOCK, 0xF00Du);
  FirEngine engine;
  engine.setFastConvolution(true);
  TEST_ASSERT_TRUE(engine.loadCoefficients(h.data(), 64));
  float out[BLOCK];
  engine.processBlock(x.data(), out);
  TEST_ASSERT_TRUE(engine.loadCoefficients(nullptr, 0));
  TEST_ASSERT_EQUAL_UINT16(0, engine.taps());
  engine.processBlock(x.data(), out);
  TEST_ASSERT_EQUAL_MEMORY(x.data(), out, sizeof(out));
}

// Unity gain: an impulse through a [1.0] filter must come out at amplitude
// 1.0 (the old firmware scaled the fast-convolution output by 0.5).
static void test_unity_gain_impulse(void) {
  float h[1] = {1.0f};
  float impulse[BLOCK] = {0};
  impulse[0] = 1.0f;
  float out[BLOCK];

  for (bool fast : {false, true}) {
    FirEngine engine;
    engine.setFastConvolution(fast);
    TEST_ASSERT_TRUE(engine.loadCoefficients(h, 1));
    engine.processBlock(impulse, out);
    char msg[64];
    snprintf(msg, sizeof(msg), "%s engine impulse response", fast ? "fast" : "direct");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-5f, 1.0f, out[0], msg);
    for (int i = 1; i < BLOCK; i++) {
      TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, out[i]);
    }
  }
}

// resetHistory clears the fast engine's overlap state: after a reset the
// engine behaves exactly as if the stream had just started.
static void test_reset_history_restarts_from_silence(void) {
  const uint16_t taps = 300;
  std::vector<float> h = randomVector(taps, 0x600D5EEDu);
  std::vector<float> x = randomVector(4 * BLOCK, 0x600DF00Du);

  FirEngine engine;
  engine.setFastConvolution(true);
  TEST_ASSERT_TRUE(engine.loadCoefficients(h.data(), taps));
  processStream(engine, x); // build up history

  engine.resetHistory();
  std::vector<float> y = processStream(engine, x);
  assertMatchesReference(y, referenceConvolution(h, x), taps, "fast after resetHistory");
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_direct_matches_reference);
  RUN_TEST(test_fast_matches_reference);
  RUN_TEST(test_direct_reload_mid_stream);
  RUN_TEST(test_fast_reload_mid_stream);
  RUN_TEST(test_zero_taps_passthrough);
  RUN_TEST(test_clear_filter_returns_to_passthrough);
  RUN_TEST(test_unity_gain_impulse);
  RUN_TEST(test_reset_history_restarts_from_silence);
  return UNITY_END();
}
