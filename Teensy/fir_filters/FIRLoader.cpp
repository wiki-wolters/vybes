#include "FIRLoader.h"
#include <SPI.h> // Usually needed for SD card

// This method loads coefficients from a file into a float array and returns a pointer to it.
// The caller is responsible for deleting the returned float array.
// The method will read the entire file to determine the number of taps.
// Returns a pointer to the coefficients array and sets actualTaps to the number of taps found.
float* FIRLoader::loadCoefficients(String filename, uint16_t& actualTaps, uint16_t maxTaps) {
    actualTaps = 0;
    if (filename == "") {
        logError("Filename cannot be empty.");
        return nullptr;
    }

    // First, count the number of coefficients in the file
    File file = SD.open(filename.c_str());
    if (!file) {
        logError("Failed to open FIR file: " + filename);
        return nullptr;
    }

    // Count the number of coefficients first
    int coeffCount = 0;
    if (filename.endsWith(".wav") || filename.endsWith(".WAV")) {
        // For WAV files, we need to parse the header to find the 'data' chunk and its size.
        // This is more robust than assuming a fixed header size.
        file.seek(0);
        if (file.size() >= 44) {
            char riff_id[4];
            char wave_id[4];
            file.seek(0);
            file.read(riff_id, 4);
            file.seek(8);
            file.read(wave_id, 4);

            if (strncmp(riff_id, "RIFF", 4) == 0 && strncmp(wave_id, "WAVE", 4) == 0) {
                file.seek(12); // Move past 'RIFF', size, and 'WAVE'
                char chunk_id[4];
                uint32_t chunk_size;
                bool dataChunkFound = false;

                while (file.available()) {
                    if (file.read(chunk_id, 4) != 4) break;
                    if (file.read(&chunk_size, 4) != 4) break;

                    if (strncmp(chunk_id, "data", 4) == 0) {
                        // Found the data chunk.
                        // We need to determine the number of samples based on bit depth.
                        // To do this, we need to read the 'fmt' chunk first.
                        // This is getting complex for just counting.
                        // Let's assume the most common case for a quick count, 
                        // and the loader will do the full validation.
                        // A better approach might be to not pre-count for WAVs.
                        // For now, we'll assume 32-bit float or 16-bit PCM for a rough estimate.
                        // Let's re-read the fmt chunk to get bit depth.
                        
                        // Re-scan to find fmt chunk to get bits per sample
                        file.seek(12);
                        bool fmtChunkFound = false;
                        uint16_t bitsPerSample = 0;
                        while(file.available()) {
                            char inner_id[4];
                            uint32_t inner_size;
                            if (file.read(inner_id, 4) != 4) break;
                            if (file.read(&inner_size, 4) != 4) break;

                            if (strncmp(inner_id, "fmt ", 4) == 0) {
                                // The bits_per_sample field is 14 bytes into the fmt chunk's data.
                                file.seek(file.position() + 14);
                                file.read((uint8_t*)&bitsPerSample, 2);
                                fmtChunkFound = true;
                                break; // Found fmt, exit inner loop
                            }
                            file.seek(file.position() + inner_size);
                            if (inner_size % 2 != 0) file.seek(file.position() + 1);
                        }

                        if (fmtChunkFound && bitsPerSample > 0) {
                            coeffCount = chunk_size / (bitsPerSample / 8);
                        } else {
                            // Fallback for safety, assume 16-bit if fmt chunk is weird
                            coeffCount = chunk_size / 2;
                        }
                        
                        dataChunkFound = true;
                        break;
                    } else {
                        // Not the data chunk, skip it.
                        file.seek(file.position() + chunk_size);
                        // Handle odd-sized chunks
                        if (chunk_size % 2 != 0) {
                            file.seek(file.position() + 1);
                        }
                    }
                }
                if (!dataChunkFound) {
                    logError("Could not find 'data' chunk to count taps in: " + filename);
                }
            } else {
                logError("Not a valid WAV file (missing RIFF/WAVE): " + filename);
            }
        }
    } else if (filename.endsWith(".txt") || filename.endsWith(".TXT")) {
        // For text files, count the number of valid number entries
        String line = "";
        while (file.available()) {
            char c = file.read();
            if (c == '\n' || c == '\r' || c == ',' || c == ' ' || c == '\t') {
                if (line.length() > 0) {
                    coeffCount++;
                    line = "";
                }
            } else if (c != '\0') {
                line += c;
            }
        }
        // Handle last coefficient if file ends without delimiter
        if (line.length() > 0) {
            coeffCount++;
        }
    } else {
        logError("Unsupported FIR file format: " + filename);
        file.close();
        return nullptr;
    }
    
    file.close();
    
    if (coeffCount <= 0) {
        logError("No valid coefficients found in file: " + filename);
        return nullptr;
    }
    
    if (maxTaps > 0 && coeffCount > maxTaps) {
        Serial.print("FIR Info: File has ");
        Serial.print(coeffCount);
        Serial.print(" taps, limiting to ");
        Serial.println(maxTaps);
        coeffCount = maxTaps;
    }
    
    // Now that we know how many coefficients we have, allocate the array
    Serial.print("FIR Info: Attempting to allocate ");
    Serial.print(coeffCount * sizeof(float));
    Serial.print(" bytes for ");
    Serial.print(coeffCount);
    Serial.println(" taps...");
    float* coeffs = new float[coeffCount];
    if (!coeffs) {
        logError("!!! MEMORY ALLOCATION FAILED !!! System will likely crash.");
        return nullptr;
    }
    logInfo("Memory allocated successfully.");
    
    // Reopen the file to read the actual coefficients
    file = SD.open(filename.c_str());
    if (!file) {
        logError("Failed to reopen FIR file: " + filename);
        delete[] coeffs;
        return nullptr;
    }
    
    // Load the coefficients
    int loadedCount = 0;
    if (filename.endsWith(".wav") || filename.endsWith(".WAV")) {
        loadedCount = loadFromWAV(file, coeffs, coeffCount);
    } else if (filename.endsWith(".txt") || filename.endsWith(".TXT")) {
        loadedCount = loadFromTXT(file, coeffs, coeffCount);
    }
    
    file.close();
    
    if (loadedCount != coeffCount) {
        logError("Mismatch in expected and loaded coefficient count");
        delete[] coeffs;
        return nullptr;
    }
    
    Serial.print("FIR Info: Successfully loaded FIR coefficients: ");
    Serial.print(filename);
    Serial.print(" (");
    Serial.print(actualTaps);
    Serial.println(" taps)");
    return coeffs;
}

// --- loadFromTXT: Loads coefficients from a text file into a float array ---
int FIRLoader::loadFromTXT(File& file, float* coeffs, int maxTaps) {
    int coeffCount = 0;
    String line = "";
    file.seek(0); // Rewind to start of file

    while (file.available() && coeffCount < maxTaps) {
        char c = file.read();

        if (c == '\n' || c == '\r' || c == ',' || c == ' ' || c == '\t') {
            if (line.length() > 0) {
                coeffs[coeffCount] = line.toFloat(); // Directly convert to float
                coeffCount++;
                line = "";
            }
        } else if (c != '\0') {
            line += c;
        }
    }

    // Handle last coefficient if file ends without delimiter
    if (line.length() > 0 && coeffCount < maxTaps) {
        coeffs[coeffCount] = line.toFloat(); // Directly convert to float
        coeffCount++;
    }

    return coeffCount;
}

// --- loadFromWAV: Loads coefficients from a WAV file into a float array ---
// Supports 32-bit float WAV (Format 3) and converts other formats
int FIRLoader::loadFromWAV(File& file, float* coeffs, int maxTaps) {
    if (file.size() < 44) {
        logError("WAV file too small (less than header size).");
        return 0;
    }

    // Read WAV header
    file.seek(0);
    // Use a struct to read header for better readability and access
    struct WavHeader {
      char riff_id[4];     // "RIFF"
      uint32_t file_size;
      char wave_id[4];     // "WAVE"
      char fmt_id[4];      // "fmt "
      uint32_t fmt_size;
      uint16_t format_type;   // 1 for PCM, 3 for IEEE Float
      uint16_t num_channels;
      uint32_t sample_rate;
      uint32_t byte_rate;
      uint16_t block_align;
      uint16_t bits_per_sample;
      // Data chunk ID and size (can be after other chunks, but for simple WAV, it's often here)
      char data_id[4];     // "data"
      uint32_t data_size;
    } header;

    if (file.read(&header, sizeof(WavHeader)) != sizeof(WavHeader)) {
        logError("Failed to read WAV header.");
        return 0;
    }

    // Basic header validation
    if (strncmp(header.riff_id, "RIFF", 4) != 0 ||
        strncmp(header.wave_id, "WAVE", 4) != 0 ||
        strncmp(header.fmt_id, "fmt ", 4) != 0) // Removed data_id check here, as data chunk might not be at fixed offset
    {
        logError("Invalid WAV file format (RIFF/WAVE/fmt chunk missing).");
        return 0;
    }

    // Search for the "data" chunk if it's not at the standard 36-byte offset
    // This is more robust as some WAV files can have extra chunks (e.g., LIST)
    uint32_t data_chunk_pos = 0;
    uint32_t data_chunk_size = 0;
    char chunk_id[4];
    uint32_t chunk_size;

    // The 'data' chunk may not immediately follow the 'fmt ' chunk.
    // We must find it by iterating through chunks. The search should start after the 'fmt ' chunk.
    // The 'fmt ' chunk starts at offset 12, has an 8-byte ID/size header, and its content is header.fmt_size bytes long.
    // So, the next chunk search should begin at offset 12 + 8 + header.fmt_size.
    uint32_t nextChunkPos = 12 + 8 + header.fmt_size;
    if (nextChunkPos % 2 != 0) { // Chunks must be word-aligned
        nextChunkPos++;
    }
    file.seek(nextChunkPos);

    while (file.available()) {
        if (file.read(chunk_id, 4) != 4) break;
        if (file.read(&chunk_size, 4) != 4) break;

        if (strncmp(chunk_id, "data", 4) == 0) {
            data_chunk_pos = file.position();
            data_chunk_size = chunk_size;
            break;
        } else {
            // Skip this chunk and its content
            file.seek(file.position() + chunk_size);
            // Ensure alignment for next chunk if chunk_size is odd
            if (chunk_size % 2 != 0) file.seek(file.position() + 1);
        }
    }

    if (data_chunk_pos == 0) {
        logError("WAV file: 'data' chunk not found.");
        return 0;
    }

    file.seek(data_chunk_pos); // Move to the beginning of the data chunk

    // Extract necessary format info from the read header struct
    uint16_t audioFormat = header.format_type;
    uint16_t numChannels = header.num_channels;
    uint16_t bitsPerSample = header.bits_per_sample;
    uint8_t bytesPerSample = bitsPerSample / 8;

    if (numChannels != 1) {
        logInfo("WAV file is not mono. Only reading coefficients from the first channel.");
        // If you strictly want mono, you'd need to skip interleaved samples.
    }

    // Check supported audio formats
    if (audioFormat != 1 && audioFormat != 3) { // 1 = PCM, 3 = IEEE Float
        Serial.print("FIR Error: Unsupported WAV audio format: ");
        Serial.print(audioFormat);
        Serial.println(". Only PCM (1) and IEEE Float (3) are supported.");
        return 0;
    }
    if (!(bitsPerSample == 8 || bitsPerSample == 16 || (bitsPerSample == 32 && audioFormat == 3))) {
        Serial.print("FIR Error: Unsupported bit depth: ");
        Serial.print(bitsPerSample);
        Serial.println(" or invalid format/bit depth combination.");
        return 0;
    }

    int coeffCount = 0;
    uint32_t bytesRead = 0; // Track bytes read from data chunk

    while (bytesRead < data_chunk_size && coeffCount < maxTaps) {
        if (bitsPerSample == 32 && audioFormat == 3) { // IEEE Float 32-bit
            float sample;
            if (file.read((uint8_t*)&sample, 4) == 4) {
                coeffs[coeffCount] = sample;
                coeffCount++;
                bytesRead += 4;
            } else { break; } // Break if read fails
        } else if (bitsPerSample == 16 && audioFormat == 1) { // PCM 16-bit
            int16_t sample;
            if (file.read((uint8_t*)&sample, 2) == 2) {
                coeffs[coeffCount] = static_cast<float>(sample) / 32768.0f; // Normalize to -1.0 to 1.0 range
                coeffCount++;
                bytesRead += 2;
            } else { break; }
        } else if (bitsPerSample == 8 && audioFormat == 1) { // PCM 8-bit
            uint8_t sample;
            if (file.read(&sample, 1) == 1) {
                coeffs[coeffCount] = static_cast<float>(sample - 128) / 128.0f; // Normalize to -1.0 to 1.0 range
                coeffCount++;
                bytesRead += 1;
            } else { break; }
        } else {
            // Should have been caught by earlier checks, but as a fallback
            logError("Unexpected format/bit depth encountered during read loop.");
            return 0;
        }

        // Skip extra channels if not mono. We only care about the first channel's data.
        if (numChannels > 1) {
            uint32_t skipBytes = (numChannels - 1) * bytesPerSample;
            if (file.position() + skipBytes > file.size()) { // Avoid seeking past end of file
                 logInfo("Reached end of file prematurely while skipping channels.");
                 break;
            }
            file.seek(file.position() + skipBytes);
            bytesRead += skipBytes;
        }
    }

    if (file.available() && coeffCount == maxTaps) { // Still data left after filling maxTaps
        Serial.print("FIR Info: WAV file has more samples than requested taps (");
        Serial.print(maxTaps);
        Serial.print("), using first ");
        Serial.println(coeffCount);
    } else if (bytesRead < data_chunk_size && coeffCount < maxTaps) {
        logError("WAV file: Premature end of data or read error.");
        return 0;
    }

    return coeffCount;
}

bool FIRLoader::isValidWAVHeader(const char* header) {
    // This helper is now less crucial as the main loadFromWAV reads the struct and checks.
    // However, it's a quick check if needed elsewhere.
    return (strncmp(header, "RIFF", 4) == 0 && strncmp(header + 8, "WAVE", 4) == 0);
}

void FIRLoader::logError(String message) {
    Serial.print("FIR Error: ");
    Serial.println(message);
}

void FIRLoader::logInfo(String message) {
    Serial.print("FIR Info: ");
    Serial.println(message);
}
