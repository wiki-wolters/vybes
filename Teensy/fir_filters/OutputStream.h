#ifndef OUTPUT_STREAM_H
#define OUTPUT_STREAM_H

#include <Print.h>
#include <stddef.h>

class OutputStream : public Print {
public:
    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const char* data, size_t len) = 0;
    virtual ~OutputStream() {}
};

#endif // OUTPUT_STREAM_H