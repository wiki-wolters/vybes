// Cycle-accurate bench for the fast-convolution FIR engine.
//
// Runs FirEngine on its own (no audio graph) so processFast can be timed with
// DWT_CYCCNT at the real 600 MHz clock, with the partition arrays living in
// the same DMAMEM/OCRAM the firmware puts them in. The point is to separate
// the two candidate costs in the complex multiply-accumulate loop:
//
//   - arithmetic: fixed cycles per complex bin, independent of filter length
//   - memory:     partSpectra + fdl are re-streamed from OCRAM every block,
//                 and at a full pool that working set is 6x the L1 D-cache
//
// A sweep over tap counts separates them. One partition is 2 KB and stays
// cache-resident, so its cycles/bin is the pure arithmetic floor; if the
// figure climbs as the working set grows past 32 KB, the loop is (also)
// memory bound. The -cold variant streams a 64 KB buffer between blocks to
// evict the cache the way the rest of the audio graph would.

#include <Arduino.h>
#include "FirEngine.h"

static constexpr uint16_t TAP_POOL = 12288;
static constexpr size_t ARENA_FLOATS =
    (((size_t)TAP_POOL + FirEngine::BLOCK_SAMPLES - 1) / FirEngine::BLOCK_SAMPLES) *
    FirEngine::FFT_SIZE * 2;

// Same placement as firArena in the firmware.
DMAMEM static float arena[ARENA_FLOATS];
// Bigger than the 32 KB L1 D-cache, so one pass evicts it.
DMAMEM static float evictor[16384];

static FirEngine eng;
static float inBlk[FirEngine::BLOCK_SAMPLES];
static float outBlk[FirEngine::BLOCK_SAMPLES];
static volatile float sink;

// One 128-sample block at 44117.647 Hz, in 600 MHz core cycles.
static constexpr float BLOCK_BUDGET = 600000000.0f * 128.0f / 44117.64706f;

static uint32_t rng = 22222;
static inline float nextRand() {
  rng = rng * 1664525u + 1013904223u;
  return (float)(int32_t)rng * (1.0f / 2147483648.0f);
}

// Decaying noise impulse response - non-trivial spectrum in every partition,
// no denormals, no accidental symmetry.
struct NoiseFeed : CoeffFeed {
  uint32_t n = 0;
  uint16_t total;
  explicit NoiseFeed(uint16_t t) : total(t) {}
  uint16_t read(float* dst, uint16_t count) override {
    for (uint16_t i = 0; i < count; i++) {
      dst[i] = nextRand() * expf(-4.0f * (float)n / (float)total) * 0.05f;
      n++;
    }
    return count;
  }
};

static void evictCache() {
  float s = 0;
  for (size_t i = 0; i < 16384; i += 8) s += evictor[i];
  sink = s;
}

static void runCase(uint16_t taps, bool cold, uint32_t blocks) {
  NoiseFeed feed(taps);
  eng.setFastConvolution(true);
  if (!eng.reservePendingIn(arena, taps) || !eng.fillPending(feed)) {
    Serial.printf("%5u taps: LOAD FAILED\n", taps);
    return;
  }
  eng.swapPending();
  eng.freeRetired();
  eng.resetHistory();

  const uint16_t parts = (taps + FirEngine::BLOCK_SAMPLES - 1) / FirEngine::BLOCK_SAMPLES;

  // Fill the frequency-domain delay line before timing anything.
  for (uint16_t i = 0; i < parts + 4; i++) eng.processBlock(inBlk, outBlk);

  firProfFwd = firProfMac = firProfInv = firProfBlocks = 0;
  uint32_t t0 = ARM_DWT_CYCCNT;
  for (uint32_t b = 0; b < blocks; b++) {
    if (cold) evictCache();
    eng.processBlock(inBlk, outBlk);
  }
  uint32_t wall = ARM_DWT_CYCCNT - t0;
  sink = outBlk[0] + outBlk[64];

  const double n = (double)blocks;
  const double mac = firProfMac / n;
  const double fwd = firProfFwd / n;
  const double inv = firProfInv / n;
  const double fir = mac + fwd + inv;
  // 127 full complex bins per partition; DC and Nyquist are handled separately.
  const double perBin = mac / ((double)parts * 127.0);

  Serial.printf("%5u %4u %s %9.0f %8.0f %8.0f %9.0f %7.2f %8.1f%%  %6.1f%%\n",
                taps, parts, cold ? "cold" : "warm",
                mac, fwd, inv, fir, perBin,
                100.0 * fir / BLOCK_BUDGET,
                100.0 * mac / fir);
  (void)wall;
}

static void report();

void setup() {
  Serial.begin(115200);

  // DWT cycle counter
  ARM_DEMCR |= ARM_DEMCR_TRCENA;
  ARM_DWT_CTRL |= ARM_DWT_CTRL_CYCCNTENA;

  for (int i = 0; i < FirEngine::BLOCK_SAMPLES; i++) inBlk[i] = nextRand() * 0.25f;
  for (size_t i = 0; i < 16384; i++) evictor[i] = 1.0f + (float)i;
}

// Repeated on a timer rather than run once at boot, so the report can be read
// by attaching to the port at any point instead of having to race the reset.
static void report() {
  Serial.println();
  Serial.println("=== Vybes fast-convolution FIR bench ===");
  Serial.printf("F_CPU %lu Hz, block budget %.0f cycles (128 smp @ 44117.6 Hz)\n",
                (unsigned long)F_CPU, BLOCK_BUDGET);
  Serial.printf("arena %u floats (%u KB) in DMAMEM/OCRAM, L1 D-cache 32 KB\n\n",
                (unsigned)ARENA_FLOATS, (unsigned)(ARENA_FLOATS * 4 / 1024));
  Serial.println(" taps part  mem  mac_cyc  fwdfft  invfft  fir_cyc  cyc/bin  %budget  mac%fir");
  Serial.println("------------------------------------------------------------------------------");

  const uint16_t sweep[] = {128, 256, 512, 1024, 2048, 4096, 8192, 12288};
  for (uint16_t t : sweep) runCase(t, false, 2000);
  Serial.println();
  for (uint16_t t : sweep) runCase(t, true, 2000);
  Serial.println("\ndone.");
}

void loop() {
  static uint32_t last = 0;
  if (Serial && (last == 0 || millis() - last > 20000)) {
    last = millis();
    report();
  }
}
