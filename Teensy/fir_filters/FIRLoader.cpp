#include "FIRLoader.h"
#ifndef VYBES_NATIVE
#include <SPI.h> // Usually needed for SD card

// SD wrappers for the core counters below; the caller keeps ownership of
// the file (position is clobbered, the file is not closed).
long FIRLoader::countWavTaps(File& file) {
    FileSource src(file);
    return countWavTaps(src, String(file.name()));
}

long FIRLoader::countTxtTaps(File& file) {
    FileSource src(file);
    return countTxtTaps(src);
}
#endif // VYBES_NATIVE

// --- Shared RIFF chunk walk ---
// The tap counter and the stream reader both need the same facts out of a
// WAV header: the fmt fields and the data chunk's position and size. One
// walk serves both (they used to carry separate copies of this loop, and
// fixes like the failed-seek guard had to land twice); what each caller
// tolerates - a missing fmt, a truncated data chunk - stays caller policy.

namespace {
struct WavHeader {
    uint16_t audioFormat = 0;
    uint16_t numChannels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    bool fmtFound = false;      // a fmt chunk of at least the 16 core bytes
    uint32_t dataPos = 0;       // first byte of sample data
    uint32_t dataSize = 0;      // declared data chunk size
    bool dataFound = false;
    bool dataTruncated = false; // declared size runs past the end of the file
};
} // namespace

// Returns false when this isn't a RIFF/WAVE file at all (too small, or the
// magic is missing); everything else is reported through the out fields.
static bool walkWavChunks(CoeffSource& src, WavHeader& h) {
    if (src.size() < 44) return false;

    char riff_id[4];
    char wave_id[4];
    src.seek(0);
    if (src.read(riff_id, 4) != 4) return false;
    src.seek(8);
    if (src.read(wave_id, 4) != 4) return false;
    if (strncmp(riff_id, "RIFF", 4) != 0 || strncmp(wave_id, "WAVE", 4) != 0) {
        return false;
    }

    src.seek(12); // Move past 'RIFF', size, and 'WAVE'
    while (src.available() && !(h.fmtFound && h.dataFound)) {
        char chunk_id[4];
        uint32_t chunk_size;
        if (src.read(chunk_id, 4) != 4) break;
        if (src.read((uint8_t*)&chunk_size, 4) != 4) break;

        if (strncmp(chunk_id, "fmt ", 4) == 0 && chunk_size >= 16) {
            uint64_t fmt_data_start = src.position();
            if (src.read((uint8_t*)&h.audioFormat, 2) != 2) break;
            if (src.read((uint8_t*)&h.numChannels, 2) != 2) break;
            if (src.read((uint8_t*)&h.sampleRate, 4) != 4) break;
            // Skip byte rate (4 bytes) and block align (2 bytes)
            src.seek(fmt_data_start + 14);
            if (src.read((uint8_t*)&h.bitsPerSample, 2) != 2) break;
            h.fmtFound = true;
            // Skip any remaining fmt chunk data (e.g. extended format info)
            if (!src.seek(fmt_data_start + chunk_size)) break;
        } else {
            if (strncmp(chunk_id, "data", 4) == 0) {
                h.dataPos = (uint32_t)src.position();
                h.dataSize = chunk_size;
                h.dataFound = true;
                // A declared size past the end of the file means the file is
                // truncated or the header is corrupt; the position past it is
                // meaningless, so stop here and let the caller reject.
                if (chunk_size > src.size() - src.position()) {
                    h.dataTruncated = true;
                    break;
                }
            }
            // Skip the chunk body ('data' included - only its size and
            // position matter). A failed seek leaves the position untouched,
            // which would re-read this same header forever - stop instead.
            if (!src.seek(src.position() + chunk_size)) break;
        }
        // Handle odd-sized chunks (must be word-aligned)
        if (chunk_size % 2 != 0) {
            src.seek(src.position() + 1);
        }
    }
    return true;
}

// See the header comment: exact frame count from the WAV header chunks only.
long FIRLoader::countWavTaps(CoeffSource& src, const String& filename) {
    WavHeader h;
    if (!walkWavChunks(src, h)) {
        logError("Not a valid WAV file (missing RIFF/WAVE): " + filename);
        return 0;
    }
    if (!h.dataFound) {
        logError("Could not find 'data' chunk to count taps in: " + filename);
        return 0;
    }
    // Report "unknown" rather than a count derived from a truncated data
    // chunk: an inflated figure would otherwise drive the shared pool
    // accounting and the load allocation. The loader rejects such a file
    // outright - a truncated impulse response is a different filter, not a
    // smaller one.
    if (h.dataTruncated) {
        logError("Data chunk (" + String(h.dataSize) +
                 " bytes) runs past the end of: " + filename);
        return 0;
    }

    if (!h.fmtFound) {
        // Fallback for safety, assume 16-bit mono if fmt chunk is weird
        return h.dataSize / 2;
    }
    // Only whole-byte sample widths can be counted (the loader supports
    // 8/16/32). Anything narrower would make the divisor below zero, so
    // report "unknown" rather than dividing by it - the caller falls back
    // to its own estimate and the reader rejects the file by bit depth.
    uint16_t channels = (h.numChannels == 0) ? 1 : h.numChannels; // safety check
    uint32_t bytesPerFrame = (uint32_t)(h.bitsPerSample / 8) * channels;
    if (h.bitsPerSample % 8 != 0 || bytesPerFrame == 0) {
        logError("Unsupported bit depth (" + String(h.bitsPerSample) +
                 ") counting taps in: " + filename);
        return 0;
    }
    return (long)(h.dataSize / bytesPerFrame);
}

// See the header comment: token count with the TXT reader's delimiter set.
// Buffered reads keep the pass fast enough to run per file in the SD
// listing (single-byte File::read calls would be an order slower).
long FIRLoader::countTxtTaps(CoeffSource& src) {
    src.seek(0);
    char buf[256];
    long count = 0;
    bool inToken = false;
    int n;
    while ((n = src.read(buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n' || c == '\r' || c == ',' || c == ' ' || c == '\t') {
                if (inToken) {
                    count++;
                    inToken = false;
                }
            } else if (c != '\0') {
                inToken = true;
            }
        }
    }
    // A token running to EOF still counts (file ends without a delimiter)
    if (inToken) {
        count++;
    }
    return count;
}

// --- Stream: count, then hand the coefficients out in order ---

long FIRLoader::Stream::begin(CoeffSource& source, const String& filename) {
    src = &source;
    format = FMT_NONE;
    count = 0;
    prepared = false;
    starvedFlag = false;
    token = "";
    bytesProcessed = 0;

    if (filename.endsWith(".wav") || filename.endsWith(".WAV")) {
        format = FMT_WAV;
        count = countWavTaps(source, filename);
    } else if (filename.endsWith(".txt") || filename.endsWith(".TXT")) {
        format = FMT_TXT;
        count = countTxtTaps(source);
    } else if (filename.endsWith(".bin") || filename.endsWith(".BIN")) {
        // Raw float32 taps, no header (matches the ESP's size/4 estimate)
        format = FMT_BIN;
        count = (long)(source.size() / 4);
    } else {
        logError("Unsupported FIR file format: " + filename);
        return 0;
    }

    if (count < 0) count = 0;
    return count;
}

bool FIRLoader::Stream::prepare() {
    if (src == nullptr || format == FMT_NONE || count <= 0) return false;
    switch (format) {
        case FMT_WAV:
            prepared = prepareWav();
            break;
        case FMT_TXT:
        case FMT_BIN:
            src->seek(0);
            token = "";
            prepared = true;
            break;
        default:
            prepared = false;
            break;
    }
    return prepared;
}

uint16_t FIRLoader::Stream::read(float* dst, uint16_t want) {
    if (!prepared || dst == nullptr || want == 0) return 0;
    switch (format) {
        case FMT_WAV: return readWav(dst, want);
        case FMT_TXT: return readTxt(dst, want);
        case FMT_BIN: return readBin(dst, want);
        default:      return 0;
    }
}

// Chunk walk for the reader: unlike the counter this insists on a fmt chunk
// and validates the sample encoding, since these are the formats the
// conversions in readWav() can actually handle.
bool FIRLoader::Stream::prepareWav() {
    WavHeader h;
    if (!walkWavChunks(*src, h)) {
        logError("Invalid WAV file (RIFF/WAVE header missing or too small)");
        return false;
    }
    if (!h.fmtFound) {
        logError("WAV file: 'fmt ' chunk not found");
        return false;
    }
    if (!h.dataFound) {
        logError("WAV file: 'data' chunk not found");
        return false;
    }
    if (h.dataTruncated) {
        logError("WAV file: data chunk runs past the end of the file");
        return false;
    }

    audioFormat = h.audioFormat;
    numChannels = h.numChannels;
    bitsPerSample = h.bitsPerSample;
    dataSize = h.dataSize;
    bytesProcessed = 0;

    // Log format information
    Serial.print("FIR Info: WAV Format - ");
    Serial.print(audioFormat == 3 ? "IEEE Float" : "PCM");
    Serial.print(", ");
    Serial.print(numChannels);
    Serial.print(" channel(s), ");
    Serial.print(h.sampleRate);
    Serial.print(" Hz, ");
    Serial.print(bitsPerSample);
    Serial.println(" bits");

    // Validate format
    if (audioFormat != 1 && audioFormat != 3) {
        Serial.print("FIR Error: Unsupported WAV audio format: ");
        Serial.println(audioFormat);
        return false;
    }

    if (bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 32) {
        Serial.print("FIR Error: Unsupported bit depth: ");
        Serial.println(bitsPerSample);
        return false;
    }

    if (audioFormat == 3 && bitsPerSample != 32) {
        logError("IEEE Float format must be 32-bit");
        return false;
    }

    if (numChannels == 0) {
        logError("Invalid number of channels (0)");
        return false;
    }

    // Warn if not mono
    if (numChannels != 1) {
        Serial.print("FIR Info: WAV file has ");
        Serial.print(numChannels);
        Serial.println(" channels. Using first channel only.");
    }

    // Position at the first sample; readWav() carries the cursor from here
    src->seek(h.dataPos);
    return true;
}

// Supports 32-bit float WAV (Format 3) and converts the PCM widths.
uint16_t FIRLoader::Stream::readWav(float* dst, uint16_t want) {
    // Bytes per sample (one channel's worth)
    uint8_t bytesPerSample = bitsPerSample / 8;

    uint16_t n = 0;
    while (n < want && bytesProcessed < dataSize) {
        float sample = 0.0f;
        bool readSuccess = false;

        // Read first channel's sample based on format
        if (audioFormat == 3 && bitsPerSample == 32) {
            // IEEE Float 32-bit
            float rawSample;
            if (src->read((uint8_t*)&rawSample, 4) == 4) {
                sample = rawSample;
                readSuccess = true;
                bytesProcessed += 4;
            }

        } else if (audioFormat == 1 && bitsPerSample == 16) {
            // PCM 16-bit signed
            int16_t rawSample;
            if (src->read((uint8_t*)&rawSample, 2) == 2) {
                // Normalize to -1.0 to +1.0
                sample = (float)rawSample / 32768.0f;
                readSuccess = true;
                bytesProcessed += 2;
            }

        } else if (audioFormat == 1 && bitsPerSample == 8) {
            // PCM 8-bit unsigned (offset by 128)
            uint8_t rawSample;
            if (src->read(&rawSample, 1) == 1) {
                // Convert from unsigned (0-255) to signed (-128 to +127), then normalize
                sample = ((float)rawSample - 128.0f) / 128.0f;
                readSuccess = true;
                bytesProcessed += 1;
            }

        } else if (audioFormat == 1 && bitsPerSample == 32) {
            // PCM 32-bit signed (less common, but supported by some tools)
            int32_t rawSample;
            if (src->read((uint8_t*)&rawSample, 4) == 4) {
                // Normalize to -1.0 to +1.0
                sample = (float)rawSample / 2147483648.0f;
                readSuccess = true;
                bytesProcessed += 4;
            }
        }

        if (!readSuccess) {
            logError("Failed to read sample data");
            starvedFlag = true;
            return n;
        }

        // Store the coefficient
        dst[n++] = sample;

        // Skip remaining channels if multi-channel
        if (numChannels > 1) {
            uint32_t skipBytes = (uint32_t)(numChannels - 1) * bytesPerSample;

            // Safety check to avoid seeking past end of data chunk
            if (bytesProcessed + skipBytes > dataSize) {
                logInfo("Reached end of data chunk while skipping channels");
                break;
            }

            src->seek(src->position() + skipBytes);
            bytesProcessed += skipBytes;
        }
    }

    if (n < want) starvedFlag = true;
    return n;
}

// Tokenizes on the delimiter set countTxtTaps counts by. A token can straddle
// two calls, so the partial one lives in 'token' between them.
uint16_t FIRLoader::Stream::readTxt(float* dst, uint16_t want) {
    uint16_t n = 0;

    while (n < want && src->available()) {
        int c = src->read();
        if (c < 0) break;

        if (c == '\n' || c == '\r' || c == ',' || c == ' ' || c == '\t') {
            if (token.length() > 0) {
                dst[n++] = token.toFloat(); // Directly convert to float
                token = "";
            }
        } else if (c != '\0') {
            token += (char)c;
        }
    }

    // Handle last coefficient if file ends without delimiter
    if (n < want && src->available() == 0 && token.length() > 0) {
        dst[n++] = token.toFloat();
        token = "";
    }

    if (n < want) starvedFlag = true;
    return n;
}

// Raw little-endian float32 taps, no header
uint16_t FIRLoader::Stream::readBin(float* dst, uint16_t want) {
    uint16_t n = 0;
    while (n < want) {
        float sample;
        if (src->read((uint8_t*)&sample, 4) != 4) break;
        dst[n++] = sample;
    }
    if (n < want) starvedFlag = true;
    return n;
}

void FIRLoader::logError(String message) {
    Serial.print("FIR Error: ");
    Serial.println(message);
}

void FIRLoader::logInfo(String message) {
    Serial.print("FIR Info: ");
    Serial.println(message);
}
