#ifndef COEFF_SOURCE_H
#define COEFF_SOURCE_H

#include <stddef.h>
#include <stdint.h>

// Minimal byte-source interface mirroring the subset of the SD File API the
// FIR loader uses, so the parse logic can run against in-memory fixtures in
// the host-native test suite. Semantics match the Teensy File class:
// read() returns the bytes actually read (or -1 at end for the single-byte
// form), seek() past the end fails and leaves the position unchanged.
class CoeffSource {
public:
    virtual ~CoeffSource() {}
    virtual int read(void* buf, size_t len) = 0;
    virtual int read() = 0;
    virtual bool seek(uint64_t pos) = 0;
    virtual uint64_t position() = 0;
    virtual int available() = 0;
    virtual uint64_t size() = 0;
};

// Pull interface for FIR coefficients, handed out in file order. The FIR
// engine builds its buffers from one of these rather than from a finished
// array (FirEngine::buildPending), so a filter is never materialised in RAM
// on its way in: at the pool limit the transient copy cost 4 bytes/tap on
// top of the engine's 16, and that was what pushed a full-pool load past the
// heap. The engine pulls one partition at a time into stack scratch.
class CoeffFeed {
public:
    virtual ~CoeffFeed() {}
    // Write up to count coefficients into dst and return how many were
    // written. A short count ends the feed - the source is exhausted or
    // unreadable - and fails the load.
    virtual uint16_t read(float* dst, uint16_t count) = 0;
};

#endif // COEFF_SOURCE_H
