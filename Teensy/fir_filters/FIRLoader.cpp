#include "FIRLoader.h"
#include <new>
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

// See the header comment: exact frame count from the WAV header chunks only.
long FIRLoader::countWavTaps(CoeffSource& src, const String& filename) {
    if (src.size() < 44) {
        return 0;
    }

    char riff_id[4];
    char wave_id[4];
    src.seek(0);
    if (src.read(riff_id, 4) != 4) return 0;
    src.seek(8);
    if (src.read(wave_id, 4) != 4) return 0;
    if (strncmp(riff_id, "RIFF", 4) != 0 || strncmp(wave_id, "WAVE", 4) != 0) {
        logError("Not a valid WAV file (missing RIFF/WAVE): " + filename);
        return 0;
    }

    src.seek(12); // Move past 'RIFF', size, and 'WAVE'
    uint16_t bitsPerSample = 0;
    uint16_t numChannels = 1;
    bool fmtChunkFound = false;
    uint32_t dataChunkSize = 0;
    bool dataChunkFound = false;

    while (src.available() && !(fmtChunkFound && dataChunkFound)) {
        char chunk_id[4];
        uint32_t chunk_size;
        if (src.read(chunk_id, 4) != 4) break;
        if (src.read(&chunk_size, 4) != 4) break;

        if (strncmp(chunk_id, "fmt ", 4) == 0) {
            uint64_t fmt_data_start = src.position();
            // numChannels is at offset 2, bitsPerSample at offset 14
            src.seek(fmt_data_start + 2);
            src.read((uint8_t*)&numChannels, 2);
            if (numChannels == 0) numChannels = 1; // Safety check
            src.seek(fmt_data_start + 14);
            src.read((uint8_t*)&bitsPerSample, 2);
            fmtChunkFound = true;
            src.seek(fmt_data_start + chunk_size);
        } else {
            if (strncmp(chunk_id, "data", 4) == 0) {
                // A declared size past the end of the file means the file is
                // truncated or the header is corrupt. Report "unknown" rather
                // than a count derived from it: an inflated figure would
                // otherwise drive the shared pool accounting and the load
                // allocation. The loader rejects such a file outright - a
                // truncated impulse response is a different filter, not a
                // shorter one.
                if (chunk_size > src.size() - src.position()) {
                    logError("Data chunk (" + String(chunk_size) +
                             " bytes) runs past the end of: " + filename);
                    return 0;
                }
                dataChunkSize = chunk_size;
                dataChunkFound = true;
            }
            // Skip the chunk body ('data' included - only its size matters).
            // A failed seek leaves the position untouched, which would re-read
            // this same header forever - stop instead.
            if (!src.seek(src.position() + chunk_size)) break;
        }
        // Handle odd-sized chunks (must be word-aligned)
        if (chunk_size % 2 != 0) {
            src.seek(src.position() + 1);
        }
    }

    if (!dataChunkFound) {
        logError("Could not find 'data' chunk to count taps in: " + filename);
        return 0;
    }

    long count;
    if (fmtChunkFound) {
        // Only whole-byte sample widths can be counted (the loader supports
        // 8/16/32). Anything narrower would make the divisor below zero, so
        // report "unknown" rather than dividing by it - the caller falls back
        // to its own estimate and the reader rejects the file by bit depth.
        uint32_t bytesPerFrame = (uint32_t)(bitsPerSample / 8) * numChannels;
        if (bitsPerSample % 8 != 0 || bytesPerFrame == 0) {
            logError("Unsupported bit depth (" + String(bitsPerSample) +
                     ") counting taps in: " + filename);
            return 0;
        }
        count = (long)(dataChunkSize / bytesPerFrame);
    } else {
        // Fallback for safety, assume 16-bit mono if fmt chunk is weird
        count = dataChunkSize / 2;
    }
    return count;
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

// Chunk walk for the reader: unlike the counter this also resolves the
// sample encoding and validates it, since these are the formats the
// conversions in readWav() can actually handle.
bool FIRLoader::Stream::prepareWav() {
    if (src->size() < 44) {
        logError("WAV file too small (less than minimum header size).");
        return false;
    }

    src->seek(0);

    // Read and validate RIFF header
    char riff_id[4];
    uint32_t file_size;
    char wave_id[4];

    if (src->read(riff_id, 4) != 4) {
        logError("Failed to read RIFF ID");
        return false;
    }
    if (src->read((uint8_t*)&file_size, 4) != 4) {
        logError("Failed to read file size");
        return false;
    }
    if (src->read(wave_id, 4) != 4) {
        logError("Failed to read WAVE ID");
        return false;
    }

    if (strncmp(riff_id, "RIFF", 4) != 0 || strncmp(wave_id, "WAVE", 4) != 0) {
        logError("Invalid WAV file format (RIFF/WAVE header missing)");
        return false;
    }

    audioFormat = 0;
    numChannels = 0;
    bitsPerSample = 0;
    dataSize = 0;
    bytesProcessed = 0;
    uint32_t sampleRate = 0;
    uint32_t dataChunkPos = 0;
    bool fmtFound = false;
    bool dataFound = false;

    // Parse all chunks to find fmt and data
    src->seek(12); // Skip past RIFF header (12 bytes: "RIFF" + size + "WAVE")

    while (src->available()) {
        char chunk_id[4];
        uint32_t chunk_size;

        if (src->read(chunk_id, 4) != 4) break;
        if (src->read((uint8_t*)&chunk_size, 4) != 4) break;

        if (strncmp(chunk_id, "fmt ", 4) == 0) {
            // Read fmt chunk
            if (chunk_size < 16) {
                logError("fmt chunk too small");
                return false;
            }

            if (src->read((uint8_t*)&audioFormat, 2) != 2) break;
            if (src->read((uint8_t*)&numChannels, 2) != 2) break;
            if (src->read((uint8_t*)&sampleRate, 4) != 4) break;

            // Skip byte rate (4 bytes) and block align (2 bytes)
            src->seek(src->position() + 6);

            if (src->read((uint8_t*)&bitsPerSample, 2) != 2) break;

            fmtFound = true;

            // Skip any remaining fmt chunk data (e.g., extended format info)
            uint32_t bytesRead = 16; // We've read 16 bytes of the fmt chunk
            if (chunk_size > bytesRead) {
                src->seek(src->position() + (chunk_size - bytesRead));
            }

            // Handle odd-sized chunks (must be word-aligned)
            if (chunk_size % 2 != 0) {
                src->seek(src->position() + 1);
            }

        } else if (strncmp(chunk_id, "data", 4) == 0) {
            // Found data chunk
            dataChunkPos = (uint32_t)src->position(); // Right after the header
            dataSize = chunk_size;
            dataFound = true;

            // Don't read the data yet - we might need to find fmt first
            // Just skip past it. A failed seek leaves the position untouched,
            // which would re-read this same header forever - stop instead.
            if (!src->seek(src->position() + chunk_size)) break;
            if (chunk_size % 2 != 0) {
                src->seek(src->position() + 1);
            }

        } else {
            // Unknown chunk, skip it (see above on the failed-seek guard)
            if (!src->seek(src->position() + chunk_size)) break;
            if (chunk_size % 2 != 0) {
                src->seek(src->position() + 1);
            }
        }

        // If we've found both chunks, we can stop searching
        if (fmtFound && dataFound) {
            break;
        }
    }

    // Validate that we found both required chunks
    if (!fmtFound) {
        logError("WAV file: 'fmt ' chunk not found");
        return false;
    }

    if (!dataFound) {
        logError("WAV file: 'data' chunk not found");
        return false;
    }

    // Log format information
    Serial.print("FIR Info: WAV Format - ");
    Serial.print(audioFormat == 3 ? "IEEE Float" : "PCM");
    Serial.print(", ");
    Serial.print(numChannels);
    Serial.print(" channel(s), ");
    Serial.print(sampleRate);
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
    src->seek(dataChunkPos);
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

// --- Whole-file load (see the header: Stream is the streaming path) ---

float* FIRLoader::loadCoefficients(CoeffSource& src, const String& filename,
                                   uint16_t& actualTaps, uint16_t maxTaps,
                                   bool truncateToMax) {
    actualTaps = 0;

    Stream stream;
    long coeffCount = stream.begin(src, filename);

    if (coeffCount <= 0) {
        logError("No valid coefficients found in file: " + filename);
        return nullptr;
    }

    if (maxTaps > 0 && coeffCount > maxTaps) {
        if (!truncateToMax) {
            // Report the requested size so the caller can relay it (capped
            // to the out-parameter's range; the exact figure is in the log)
            actualTaps = (coeffCount > 65535) ? 65535 : (uint16_t)coeffCount;
            logError("File " + filename + " has " + String(coeffCount) +
                     " taps but only " + String(maxTaps) + " fit - load rejected");
            return nullptr;
        }
        Serial.print("FIR Info: File has ");
        Serial.print(coeffCount);
        Serial.print(" taps, limiting to ");
        Serial.println(maxTaps);
        coeffCount = maxTaps;
    }

    // Uncapped callers (maxTaps == 0) take coeffCount straight from the file
    // header, where a corrupt size can name more taps than actualTaps can
    // report or the allocation below can size. Refuse instead of wrapping.
    if (coeffCount > 65535) {
        actualTaps = 65535;
        logError("File " + filename + " has " + String(coeffCount) +
                 " taps - more than the 65535 one filter can hold");
        return nullptr;
    }

    if (!stream.prepare()) {
        // Unsupported sample encoding - the count was readable, the data isn't
        return nullptr;
    }

    // Now that we know how many coefficients we have, allocate the array
    Serial.print("FIR Info: Attempting to allocate ");
    Serial.print(coeffCount * sizeof(float));
    Serial.print(" bytes for ");
    Serial.print(coeffCount);
    Serial.println(" taps...");
    // nothrow: the Teensy core's operator new returns nullptr rather than
    // throwing, but the compiler assumes throwing-new can't - without
    // std::nothrow this null check is dead code.
    float* coeffs = new (std::nothrow) float[coeffCount];
    if (!coeffs) {
        logError("Allocation failed (" + String((unsigned long)(coeffCount * sizeof(float))) +
                 " bytes) - load rejected: " + filename);
        return nullptr;
    }
    logInfo("Memory allocated successfully.");

    if (stream.read(coeffs, (uint16_t)coeffCount) != (uint16_t)coeffCount) {
        logError("Mismatch in expected and loaded coefficient count");
        delete[] coeffs;
        return nullptr;
    }

    actualTaps = (uint16_t)coeffCount;

    // Coefficients are used verbatim - no normalization or scaling - so the
    // filter applies exactly the response designed in the file.
    Serial.print("FIR Info: Successfully loaded FIR coefficients: ");
    Serial.print(filename);
    Serial.print(" (");
    Serial.print(actualTaps);
    Serial.println(" taps)");
    return coeffs;
}

void FIRLoader::logError(String message) {
    Serial.print("FIR Error: ");
    Serial.println(message);
}

void FIRLoader::logInfo(String message) {
    Serial.print("FIR Info: ");
    Serial.println(message);
}
