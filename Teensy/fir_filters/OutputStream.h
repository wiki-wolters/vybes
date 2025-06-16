#ifndef OUTPUT_STREAM_H
#define OUTPUT_STREAM_H

#include <stddef.h>

class OutputStream {
public:
    virtual size_t write(const char* data, size_t len) = 0;
    virtual ~OutputStream() {}
};

#endif // OUTPUT_STREAM_H
