#include "FIRLoader.h"
#include <SPI.h> // Usually needed for SD card

// This method loads coefficients from a file into a float array and returns a pointer to it.
// The caller is responsible for deleting the returned float array.
// The method will read the entire file to determine the number of taps.
// Returns a pointer to the coefficients array and sets actualTaps to the number of taps found.
float* FIRLoader::loadCoefficients(String filename, uint16_t& actualTaps) {
    actualTaps = 0; // Initialize actualTaps to 0
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
        // For WAV files, we'll count the number of samples in the data chunk
        if (file.size() >= 44) { // Minimum valid WAV header size
            file.seek(40); // Points to the data chunk size (4 bytes after 'data' marker)
            uint32_t dataSize = 0;
            for (int i = 0; i < 4; i++) {
                dataSize |= (file.read() << (i * 8));
            }
            // Assuming 32-bit float samples (4 bytes per sample)
            coeffCount = dataSize / 4;
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
    
    // Now that we know how many coefficients we have, allocate the array
    float* coeffs = new float[coeffCount];
    if (!coeffs) {
        logError("Failed to allocate memory for coefficients.");
        return nullptr;
    }
    
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
    
    actualTaps = coeffCount;
    logInfo("Successfully loaded FIR coefficients: " + filename + " (" + String(actualTaps) + " taps)");
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

    // Start searching after the fmt chunk (after the 36 bytes of typical header + fmt_size)
    file.seek(sizeof(WavHeader) - 8); // Go back to where data_id usually is if it's not a standard header.
                                      // Or even better, just iterate from after the fmt_chunk
    file.seek(12 + (4 + header.fmt_size)); // 12 bytes for RIFF/WAVE/fmt+size. Position after fmt chunk

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
        logError("Unsupported WAV audio format: " + String(audioFormat) + ". Only PCM (1) and IEEE Float (3) are supported.");
        return 0;
    }
    if (!(bitsPerSample == 8 || bitsPerSample == 16 || (bitsPerSample == 32 && audioFormat == 3))) {
        logError("Unsupported bit depth: " + String(bitsPerSample) + " or invalid format/bit depth combination.");
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
        logInfo("WAV file has more samples than requested taps (" + String(maxTaps) + "), using first " + String(coeffCount));
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
    Serial.println("FIR Error: " + message);
}

void FIRLoader::logInfo(String message) {
    Serial.println("FIR Info: " + message);
}