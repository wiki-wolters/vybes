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

#endif // COEFF_SOURCE_H
