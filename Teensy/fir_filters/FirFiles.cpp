#include "FirFiles.h"

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

#include "SketchState.h"
#include "FIRLoader.h"
#include "FirEngine.h"
#include "teensy_protocol.h" // FIR_POOL_CHARGE_QUANTUM

// Machine-readable companion to the human-readable "ERROR FIR ..." lines, so
// the ESP can attribute a failure to a channel and surface it in the web UI
// instead of it dying in a debug console. Codes: nosd, missing, poolfull,
// toobig, nomem.
static void reportFirError(int ch, const char* code, const char* file) {
  Serial1.printf("FIRERR %d %s %s\n", ch, code, file);
}

// One fixed block holding every loaded filter's working buffers, sliced
// across the channels at each load. Reserved statically rather than
// allocated per load: a full pool needs ~206KB in one contiguous run, and
// re-requesting that only works while the heap is still pristine. Turning
// one filter off and back on frees the block, asks for a smaller one, then
// asks for a bigger one again - and by then no single run that size is left,
// so the request fails, the per-filter fallback fits all but the last
// filter, and that output runs uncorrected (the FIRERR nomem on output 1 of
// a 3072/3072/6144 set). Holding the worst case at a fixed address costs the
// same RAM2 a full pool always took and makes every set the pool check
// accepts fit by construction - no allocation left to fail, no fallback.
//
// Sized in whole partitions because the pool is CHARGED in whole partitions
// (FIR_POOL_CHARGE_QUANTUM in teensy_protocol.h, matched by the ESP's
// accounting): fast convolution rounds each filter up to a 128-tap
// partition costing 2 x FFT_SIZE floats, so with partition-quantized
// charging the pool's partition count is exactly the arena's worst case -
// no per-channel rounding waste can exceed what was charged. Sizing for the
// raw tap count plus one partial partition per channel instead cost 14KB
// more, and that 14KB was the RAM2 headroom whose loss made the RTA's boot
// allocation fail (see RtaFFT4096.cpp). The direct engine needs far less
// per channel, so sizing for fast convolution covers both.
static_assert(FIR_POOL_CHARGE_QUANTUM == FirEngine::BLOCK_SAMPLES,
              "pool charging quantum must match the engine partition size");
static constexpr size_t FIR_ARENA_FLOATS =
    (((size_t)FIR_TAP_POOL + FirEngine::BLOCK_SAMPLES - 1) / FirEngine::BLOCK_SAMPLES) *
    FirEngine::FFT_SIZE * 2;
DMAMEM static float firArena[FIR_ARENA_FLOATS];

// Clears every filter, so the slices of firArena they hold go unreferenced
// before the next load re-carves it.
static void releaseFirBuffers() {
  for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
    firFilter[ch].loadCoefficients(nullptr, 0);
    state.outputs[ch].firTaps = 0;
  }
}

// Streams one output's file into the buffers already reserved for it. The
// engine pulls one 128-tap partition at a time, so coefficients never exist
// outside its buffers as more than 512 bytes of its own stack scratch, never
// a copy of the filter. 'taps' is the count the sizing pass accepted.
static bool fillFirChannel(int ch, uint16_t taps) {
  OutputState& o = state.outputs[ch];

  File file = SD.open(o.firFile);
  if (!file) {
    Serial1.printf("ERROR FIR load failed: %s (output %d)\n", o.firFile, ch);
    reportFirError(ch, "missing", o.firFile);
    return false;
  }
  FIRLoader::FileSource source(file);
  FIRLoader::Stream stream;

  // prepare() is where an encoding the reader can't convert is caught; the
  // sizing pass only needed the chunk headers.
  if (stream.begin(source, o.firFile) != (long)taps || !stream.prepare()) {
    file.close();
    Serial1.printf("ERROR FIR load failed: %s (output %d)\n", o.firFile, ch);
    reportFirError(ch, "missing", o.firFile);
    return false;
  }

  bool loaded = firFilter[ch].fillReserved(stream);
  file.close();
  if (!loaded) {
    Serial1.printf("ERROR FIR load failed: unreadable file %s (output %d)\n", o.firFile, ch);
    reportFirError(ch, "missing", o.firFile);
    return false;
  }

  o.firTaps = taps;
  return true;
}

void loadFirFiles() {
  // Note: incoming serial commands are buffered by the UART while we read
  // from the SD card, so no special handling is needed here.
  if (!sdReady()) {
    Serial.println("SD not available - can't load FIR files");
    // Clear any existing FIR filters to ensure no stale filters are used
    releaseFirBuffers();
    for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
      if (state.outputs[ch].firFile[0] != '\0') {
        reportFirError(ch, "nosd", state.outputs[ch].firFile);
      }
    }
    applyDelays();
    return;
  }

  // Three passes: size every file, carve the arena into a slice per output,
  // then read the files in. Sizing has to come first because the pool is
  // shared - what fits depends on the whole set, not on one file - and the
  // reads have to come last because an SD open between two reservations used
  // to cut up the free space they needed (see firArena, which is now static
  // so the slicing cannot fail either way).
  printMemoryStats("before FIR loads");
  releaseFirBuffers();

  // Pass 1: size every file (header reads only - no coefficients yet) and
  // spend the pool in channel order, so which outputs get rejected when a
  // set over-subscribes stays independent of the load order chosen below.
  long wantTaps[NUM_OUTPUTS] = {0};
  uint32_t poolUsed = 0;
  for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
    OutputState& o = state.outputs[ch];
    if (o.firFile[0] == '\0') continue;

    uint32_t remaining = FIR_TAP_POOL - poolUsed;
    if (remaining == 0) {
      Serial1.printf("ERROR FIR pool exhausted, skipping %s (output %d)\n", o.firFile, ch);
      reportFirError(ch, "poolfull", o.firFile);
      continue;
    }

    File file = SD.open(o.firFile);
    if (!file) {
      Serial1.printf("ERROR FIR load failed: %s (output %d)\n", o.firFile, ch);
      reportFirError(ch, "missing", o.firFile);
      continue;
    }
    FIRLoader::FileSource source(file);
    FIRLoader::Stream stream;
    long fileTaps = stream.begin(source, o.firFile);
    file.close();

    if (fileTaps <= 0) {
      Serial1.printf("ERROR FIR load failed: %s (output %d)\n", o.firFile, ch);
      reportFirError(ch, "missing", o.firFile);
      continue;
    }
    // Charged in whole partitions - what the arena actually spends (and how
    // the ESP accounts the pool; see FIR_POOL_CHARGE_QUANTUM). A file that
    // doesn't fit the remaining pool is rejected outright rather than
    // truncated - a shortened impulse response is a different filter, not a
    // smaller one.
    uint32_t charged = ((uint32_t)fileTaps + FIR_POOL_CHARGE_QUANTUM - 1) /
                       FIR_POOL_CHARGE_QUANTUM * FIR_POOL_CHARGE_QUANTUM;
    if (charged > remaining) {
      Serial1.printf("ERROR FIR pool exceeded: %s needs %lu taps (%ld padded to whole partitions), %lu of %u left (output %d)\n",
                     o.firFile, (unsigned long)charged, fileTaps,
                     (unsigned long)remaining, FIR_TAP_POOL, ch);
      reportFirError(ch, "toobig", o.firFile);
      continue;
    }
    wantTaps[ch] = fileTaps;
    poolUsed += charged;
  }

  // Pass 2: carve the arena up in channel order. Nothing here can fail for
  // want of memory - the arena is sized for the worst case pass 1 can accept
  // - so which outputs load no longer depends on how the heap happens to
  // look, and a channel is never dropped for being last in line.
  uint16_t reservedTaps[NUM_OUTPUTS] = {0};
  size_t arenaUsed = 0;
  for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
    if (wantTaps[ch] == 0) continue;
    uint16_t taps = (uint16_t)wantTaps[ch];
    size_t need = firFilter[ch].reservedFloats(taps);

    // Unreachable unless the arena and the pool check disagree; slicing past
    // the end would be a buffer overrun, so refuse the channel instead.
    if (arenaUsed + need > FIR_ARENA_FLOATS ||
        !firFilter[ch].reserveCoefficientsIn(firArena + arenaUsed, taps)) {
      Serial1.printf("ERROR FIR arena exhausted: %s needs %lu floats, %lu of %lu used (output %d)\n",
                     state.outputs[ch].firFile, (unsigned long)need,
                     (unsigned long)arenaUsed, (unsigned long)FIR_ARENA_FLOATS, ch);
      reportFirError(ch, "nomem", state.outputs[ch].firFile);
      continue;
    }
    arenaUsed += need;
    reservedTaps[ch] = taps;
  }

  // Pass 3: fill what was reserved. A file that can't be read now gives up
  // only its own slice.
  poolUsed = 0;
  for (int ch = 0; ch < NUM_OUTPUTS; ch++) {
    if (reservedTaps[ch] == 0) continue;
    if (fillFirChannel(ch, reservedTaps[ch])) {
      poolUsed += reservedTaps[ch];
      Serial.printf("Output %d FIR loaded: %s (%u taps, pool %lu/%u)\n",
                    ch, state.outputs[ch].firFile, reservedTaps[ch], (unsigned long)poolUsed,
                    FIR_TAP_POOL);
    }
  }

  printMemoryStats("after FIR loads");

  // FIR latencies may have changed - realign the channels
  applyDelays();
}
