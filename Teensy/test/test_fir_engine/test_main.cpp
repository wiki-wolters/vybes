// FIR engine equivalence tests: both the direct CMSIS path and the
// uniformly partitioned overlap-save fast-convolution path must produce the
// same output as an independent naive convolution computed in double
// precision, block by block, for tap counts spanning partition boundaries.

#include <unity.h>

#include <cmath>
#include <cstdio>  // snprintf: libc++ pulls this in transitively, libstdc++ does not
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
// --- Streaming loads (CoeffFeed) ---
// The engine pulls the coefficients in instead of copying a finished array,
// so nothing filter-sized has to exist alongside its buffers. Output must be
// identical to the array path, and the pull must stay partition-sized.

// Serves a vector through CoeffFeed, recording the largest bite the engine
// asked for and optionally cutting the feed short partway through.
class VectorFeed : public CoeffFeed {
public:
  VectorFeed(const std::vector<float>& h, size_t limit = SIZE_MAX)
    : h(h), limit(limit < h.size() ? limit : h.size()) {}

  uint16_t read(float* dst, uint16_t count) override {
    if (count > maxRequest) maxRequest = count;
    size_t left = limit - pos;
    if ((size_t)count > left) count = (uint16_t)left;
    memcpy(dst, h.data() + pos, (size_t)count * sizeof(float));
    pos += count;
    return count;
  }

  uint16_t maxRequest = 0;

private:
  const std::vector<float>& h;
  size_t limit;
  size_t pos = 0;
};

static void runStreamedMatchesArray(bool fast) {
  for (uint16_t taps : kTapCounts) {
    size_t blocks = (size_t)(taps / BLOCK) + 6;
    std::vector<float> h = randomVector(taps, 0xC0FFEE00u + taps);
    std::vector<float> x = randomVector(blocks * BLOCK, 0xBEEF0000u + taps);

    FirEngine arrayEngine, streamedEngine;
    arrayEngine.setFastConvolution(fast);
    streamedEngine.setFastConvolution(fast);

    VectorFeed feed(h);
    TEST_ASSERT_TRUE(arrayEngine.loadCoefficients(h.data(), taps));
    TEST_ASSERT_TRUE(streamedEngine.loadCoefficients(feed, taps));
    TEST_ASSERT_EQUAL_UINT16(taps, streamedEngine.taps());

    // The fast engine transforms one partition at a time, so it must never
    // ask for more than a partition at once - that bound is the whole point
    // of the streaming path.
    if (fast) {
      TEST_ASSERT_LESS_OR_EQUAL_UINT16(FirEngine::BLOCK_SAMPLES, feed.maxRequest);
    }

    std::vector<float> fromArray = processStream(arrayEngine, x);
    std::vector<float> fromStream = processStream(streamedEngine, x);
    for (size_t i = 0; i < fromArray.size(); i++) {
      TEST_ASSERT_EQUAL_FLOAT(fromArray[i], fromStream[i]);
    }
  }
}

static void test_direct_streamed_matches_array(void) { runStreamedMatchesArray(false); }
static void test_fast_streamed_matches_array(void) { runStreamedMatchesArray(true); }

// A feed that runs dry mid-filter is a broken file, not a shorter filter:
// the load fails and the filter already playing carries on undisturbed -
// same coefficients, same history. Checked against a control engine that
// never sees the failed load, so a nudged filter state fails the test too.
static void runShortFeedKeepsCurrentFilter(bool fast) {
  const uint16_t taps = 500;
  const size_t blocks = 8;
  std::vector<float> h = randomVector(taps, 0x5407u);
  std::vector<float> other = randomVector(taps, 0x5409u);
  std::vector<float> x = randomVector(blocks * BLOCK, 0x5408u);

  FirEngine control, engine;
  control.setFastConvolution(fast);
  engine.setFastConvolution(fast);
  TEST_ASSERT_TRUE(control.loadCoefficients(h.data(), taps));
  TEST_ASSERT_TRUE(engine.loadCoefficients(h.data(), taps));

  std::vector<float> yControl(x.size()), yEngine(x.size());
  for (size_t off = 0; off < x.size(); off += BLOCK) {
    if (off == (blocks / 2) * BLOCK) {
      VectorFeed truncated(other, taps - 10); // dries up before the last partition
      TEST_ASSERT_FALSE(engine.loadCoefficients(truncated, taps));
      TEST_ASSERT_EQUAL_UINT16(taps, engine.taps());
    }
    control.processBlock(x.data() + off, yControl.data() + off);
    engine.processBlock(x.data() + off, yEngine.data() + off);
  }

  for (size_t i = 0; i < x.size(); i++) {
    TEST_ASSERT_EQUAL_FLOAT(yControl[i], yEngine[i]);
  }
}

static void test_direct_short_feed_keeps_current_filter(void) {
  runShortFeedKeepsCurrentFilter(false);
}
static void test_fast_short_feed_keeps_current_filter(void) {
  runShortFeedKeepsCurrentFilter(true);
}

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

// --- Reserve / fill split ---
// A caller loading several filters claims every buffer before any file I/O,
// so the big partition arrays land contiguously. The two halves together
// must be indistinguishable from a single load.

static void runReserveThenFillMatchesLoad(bool fast) {
  for (uint16_t taps : kTapCounts) {
    size_t blocks = (size_t)(taps / BLOCK) + 6;
    std::vector<float> h = randomVector(taps, 0xC0FFEE00u + taps);
    std::vector<float> x = randomVector(blocks * BLOCK, 0xBEEF0000u + taps);

    FirEngine oneShot, split;
    oneShot.setFastConvolution(fast);
    split.setFastConvolution(fast);

    VectorFeed feedA(h), feedB(h);
    TEST_ASSERT_TRUE(oneShot.loadCoefficients(feedA, taps));

    TEST_ASSERT_TRUE(split.reservePending(taps));
    TEST_ASSERT_EQUAL_UINT16(0, split.taps()); // nothing swapped in yet
    TEST_ASSERT_TRUE(split.fillPending(feedB));
    split.swapPending();
    split.freeRetired();
    TEST_ASSERT_EQUAL_UINT16(taps, split.taps());

    std::vector<float> a = processStream(oneShot, x);
    std::vector<float> b = processStream(split, x);
    for (size_t i = 0; i < a.size(); i++) TEST_ASSERT_EQUAL_FLOAT(a[i], b[i]);
  }
}

static void test_direct_reserve_then_fill_matches_load(void) {
  runReserveThenFillMatchesLoad(false);
}
static void test_fast_reserve_then_fill_matches_load(void) {
  runReserveThenFillMatchesLoad(true);
}

// A fill that comes up short releases its own reservation, so a later
// reservation on the same engine starts clean and the running filter is
// untouched throughout.
static void test_failed_fill_releases_its_reservation(void) {
  const uint16_t taps = 500;
  std::vector<float> h = randomVector(taps, 0x7A1150u);
  std::vector<float> x = randomVector(4 * BLOCK, 0x7A1151u);

  FirEngine engine;
  engine.setFastConvolution(true);
  VectorFeed good(h);
  TEST_ASSERT_TRUE(engine.loadCoefficients(good, taps));
  std::vector<float> before = processStream(engine, x);

  std::vector<float> other = randomVector(taps, 0x7A1152u);
  VectorFeed truncated(other, taps - 10);
  TEST_ASSERT_TRUE(engine.reservePending(taps));
  TEST_ASSERT_FALSE(engine.fillPending(truncated));

  // The reservation is gone, so a swap now is a no-op and the filter stands
  engine.swapPending();
  TEST_ASSERT_EQUAL_UINT16(taps, engine.taps());
  engine.resetHistory();
  std::vector<float> after = processStream(engine, x);
  for (size_t i = 0; i < before.size(); i++) TEST_ASSERT_EQUAL_FLOAT(before[i], after[i]);

  // And the engine still takes a fresh reservation afterwards
  VectorFeed again(other);
  TEST_ASSERT_TRUE(engine.reservePending(taps));
  TEST_ASSERT_TRUE(engine.fillPending(again));
}

// Filling without reserving is refused rather than writing through nulls,
// and a discarded reservation never reaches the filter.
static void test_fill_without_reserve_and_discard(void) {
  const uint16_t taps = 256;
  std::vector<float> h = randomVector(taps, 0xD15CA2Du);

  FirEngine engine;
  engine.setFastConvolution(true);
  VectorFeed feed(h);
  TEST_ASSERT_FALSE(engine.fillPending(feed));

  TEST_ASSERT_TRUE(engine.reservePending(taps));
  engine.discardPending();
  engine.swapPending();
  TEST_ASSERT_EQUAL_UINT16(0, engine.taps()); // discarded, never swapped in
}

// --- Caller-owned storage ---
// A whole set of filters is carved out of one block, so the allocator's
// per-request padding is paid once. The engine must read and write its slice
// exactly as it would its own buffers, and never free it.

static void runReserveInMatchesOwned(bool fast) {
  for (uint16_t taps : kTapCounts) {
    size_t blocks = (size_t)(taps / BLOCK) + 6;
    std::vector<float> h = randomVector(taps, 0xC0FFEE00u + taps);
    std::vector<float> x = randomVector(blocks * BLOCK, 0xBEEF0000u + taps);

    FirEngine owned, sliced;
    owned.setFastConvolution(fast);
    sliced.setFastConvolution(fast);

    // A guard word on each side catches a slice that writes out of bounds
    size_t need = sliced.pendingFloats(taps);
    TEST_ASSERT_GREATER_THAN_UINT32(0, need);
    std::vector<float> block(need + 2, 12345.0f);

    VectorFeed feedA(h), feedB(h);
    TEST_ASSERT_TRUE(owned.loadCoefficients(feedA, taps));
    TEST_ASSERT_TRUE(sliced.reservePendingIn(block.data() + 1, taps));
    TEST_ASSERT_TRUE(sliced.fillPending(feedB));
    sliced.swapPending();
    sliced.freeRetired();

    TEST_ASSERT_EQUAL_FLOAT(12345.0f, block.front());
    TEST_ASSERT_EQUAL_FLOAT(12345.0f, block.back());

    std::vector<float> a = processStream(owned, x);
    std::vector<float> b = processStream(sliced, x);
    for (size_t i = 0; i < a.size(); i++) TEST_ASSERT_EQUAL_FLOAT(a[i], b[i]);
  }
}

static void test_direct_reserve_in_matches_owned(void) { runReserveInMatchesOwned(false); }
static void test_fast_reserve_in_matches_owned(void) { runReserveInMatchesOwned(true); }

// Several filters sharing one block must not tread on each other, and none
// of them may free any of it - the block outlives the engines here, so a
// stray delete[] would corrupt the heap under the test.
static void test_shared_block_across_filters(void) {
  const uint16_t tapsA = 700, tapsB = 300, tapsC = 128;
  std::vector<float> hA = randomVector(tapsA, 0x5A1u);
  std::vector<float> hB = randomVector(tapsB, 0x5A2u);
  std::vector<float> hC = randomVector(tapsC, 0x5A3u);
  std::vector<float> x = randomVector(8 * BLOCK, 0x5A4u);

  FirEngine a, b, c;
  for (FirEngine* e : {&a, &b, &c}) e->setFastConvolution(true);

  size_t nA = a.pendingFloats(tapsA), nB = b.pendingFloats(tapsB), nC = c.pendingFloats(tapsC);
  std::vector<float> block(nA + nB + nC);

  VectorFeed fA(hA), fB(hB), fC(hC);
  TEST_ASSERT_TRUE(a.reservePendingIn(block.data(), tapsA));
  TEST_ASSERT_TRUE(b.reservePendingIn(block.data() + nA, tapsB));
  TEST_ASSERT_TRUE(c.reservePendingIn(block.data() + nA + nB, tapsC));
  TEST_ASSERT_TRUE(a.fillPending(fA));
  TEST_ASSERT_TRUE(b.fillPending(fB));
  TEST_ASSERT_TRUE(c.fillPending(fC));
  for (FirEngine* e : {&a, &b, &c}) { e->swapPending(); e->freeRetired(); }

  // Each must still convolve its own filter, unperturbed by its neighbours
  struct { FirEngine* e; std::vector<float>* h; uint16_t taps; } cases[] = {
    {&a, &hA, tapsA}, {&b, &hB, tapsB}, {&c, &hC, tapsC},
  };
  for (auto& t : cases) {
    std::vector<float> y = processStream(*t.e, x);
    assertMatchesReference(y, referenceConvolution(*t.h, x), t.taps, "shared block");
  }
}

// Clearing a filter that lives in caller storage must not free the block.
static void test_clearing_sliced_filter_leaves_block_intact(void) {
  const uint16_t taps = 256;
  std::vector<float> h = randomVector(taps, 0xC1EA2u);

  FirEngine engine;
  engine.setFastConvolution(true);
  std::vector<float> block(engine.pendingFloats(taps));

  VectorFeed feed(h);
  TEST_ASSERT_TRUE(engine.reservePendingIn(block.data(), taps));
  TEST_ASSERT_TRUE(engine.fillPending(feed));
  engine.swapPending();
  engine.freeRetired();
  TEST_ASSERT_EQUAL_UINT16(taps, engine.taps());

  TEST_ASSERT_TRUE(engine.loadCoefficients(nullptr, 0)); // retires the slice
  TEST_ASSERT_EQUAL_UINT16(0, engine.taps());
  // block is still ours: writing it must be safe, and freeing it is the
  // vector's job at scope exit (a double free would trip the allocator)
  block.assign(block.size(), 1.0f);
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
  RUN_TEST(test_direct_streamed_matches_array);
  RUN_TEST(test_fast_streamed_matches_array);
  RUN_TEST(test_direct_short_feed_keeps_current_filter);
  RUN_TEST(test_fast_short_feed_keeps_current_filter);
  RUN_TEST(test_direct_reserve_then_fill_matches_load);
  RUN_TEST(test_fast_reserve_then_fill_matches_load);
  RUN_TEST(test_failed_fill_releases_its_reservation);
  RUN_TEST(test_fill_without_reserve_and_discard);
  RUN_TEST(test_direct_reserve_in_matches_owned);
  RUN_TEST(test_fast_reserve_in_matches_owned);
  RUN_TEST(test_shared_block_across_filters);
  RUN_TEST(test_clearing_sliced_filter_leaves_block_intact);
  return UNITY_END();
}
