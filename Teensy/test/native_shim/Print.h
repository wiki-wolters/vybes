#ifndef NATIVE_SHIM_PRINT_H
#define NATIVE_SHIM_PRINT_H

// Minimal host-side stand-in for the Teensy Print class. Mirrors the
// virtual/non-virtual split of the real class so OutputStream compiles
// unchanged.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "WString.h"

class Print {
public:
    virtual ~Print() {}

    virtual size_t write(uint8_t b) = 0;
    virtual size_t write(const uint8_t* buffer, size_t size) {
        size_t n = 0;
        while (size--) n += write(*buffer++);
        return n;
    }
    size_t write(const char* str) {
        return str ? write((const uint8_t*)str, strlen(str)) : 0;
    }
    size_t write(const char* buffer, size_t size) {
        return write((const uint8_t*)buffer, size);
    }

    size_t print(const char* s) { return write(s); }
    size_t print(char c) { return write((uint8_t)c); }
    size_t print(const String& s) { return write(s.c_str()); }
    size_t print(int v) { return printNumber("%d", v); }
    size_t print(unsigned int v) { return printNumber("%u", v); }
    size_t print(long v) { return printNumber("%ld", v); }
    size_t print(unsigned long v) { return printNumber("%lu", v); }
    size_t print(double v) { char b[40]; snprintf(b, sizeof(b), "%.2f", v); return write(b); }

    size_t println() { return write("\n"); }
    template <typename T>
    size_t println(const T& v) { return print(v) + println(); }

private:
    template <typename T>
    size_t printNumber(const char* fmt, T v) {
        char b[24];
        snprintf(b, sizeof(b), fmt, v);
        return write(b);
    }
};

#endif // NATIVE_SHIM_PRINT_H
