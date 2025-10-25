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
                        
                        // Re-scan to find fmt chunk to get bits per sample and num channels
                        file.seek(12);
                        bool fmtChunkFound = false;
                        uint16_t bitsPerSample = 0;
                        uint16_t numChannels = 1;
                        while(file.available()) {
                            char inner_id[4];
                            uint32_t inner_size;
                            if (file.read(inner_id, 4) != 4) break;
                            if (file.read(&inner_size, 4) != 4) break;

                            if (strncmp(inner_id, "fmt ", 4) == 0) {
                                long fmt_data_start = file.position();
                                // Read numChannels (at offset 2)
                                file.seek(fmt_data_start + 2);
                                file.read((uint8_t*)&numChannels, 2);
                                if (numChannels == 0) numChannels = 1; // Safety check

                                // Read bitsPerSample (at offset 14)
                                file.seek(fmt_data_start + 14);
                                file.read((uint8_t*)&bitsPerSample, 2);
                                
                                fmtChunkFound = true;
                                break; // Found fmt, exit inner loop
                            }
                            file.seek(file.position() + inner_size);
                            if (inner_size % 2 != 0) file.seek(file.position() + 1);
                        }

                        if (fmtChunkFound && bitsPerSample > 0) {
                            coeffCount = chunk_size / (bitsPerSample / 8);
                            if (numChannels > 0) {
                                coeffCount /= numChannels;
                            }
                        } else {
                            // Fallback for safety, assume 16-bit mono if fmt chunk is weird
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

    actualTaps = loadedCount;
    
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
// --- loadFromWAV: Loads coefficients from a WAV file into a float array ---
// Supports 32-bit float WAV (Format 3) and converts other formats
// Fixed version with proper chunk parsing and multi-channel handling
int FIRLoader::loadFromWAV(File& file, float* coeffs, int maxTaps) {
    if (file.size() < 44) {
        logError("WAV file too small (less than minimum header size).");
        return 0;
    }

    file.seek(0);

    // Read and validate RIFF header
    char riff_id[4];
    uint32_t file_size;
    char wave_id[4];
    
    if (file.read(riff_id, 4) != 4) {
        logError("Failed to read RIFF ID");
        return 0;
    }
    if (file.read((uint8_t*)&file_size, 4) != 4) {
        logError("Failed to read file size");
        return 0;
    }
    if (file.read(wave_id, 4) != 4) {
        logError("Failed to read WAVE ID");
        return 0;
    }

    if (strncmp(riff_id, "RIFF", 4) != 0 || strncmp(wave_id, "WAVE", 4) != 0) {
        logError("Invalid WAV file format (RIFF/WAVE header missing)");
        return 0;
    }

    // Initialize format parameters
    uint16_t audioFormat = 0;
    uint16_t numChannels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    bool fmtFound = false;
    
    uint32_t dataChunkPos = 0;
    uint32_t dataChunkSize = 0;
    bool dataFound = false;

    // Parse all chunks to find fmt and data
    file.seek(12); // Skip past RIFF header (12 bytes: "RIFF" + size + "WAVE")
    
    while (file.available()) {
        char chunk_id[4];
        uint32_t chunk_size;
        
        if (file.read(chunk_id, 4) != 4) break;
        if (file.read((uint8_t*)&chunk_size, 4) != 4) break;
        
        if (strncmp(chunk_id, "fmt ", 4) == 0) {
            // Read fmt chunk
            if (chunk_size < 16) {
                logError("fmt chunk too small");
                return 0;
            }
            
            if (file.read((uint8_t*)&audioFormat, 2) != 2) break;
            if (file.read((uint8_t*)&numChannels, 2) != 2) break;
            if (file.read((uint8_t*)&sampleRate, 4) != 4) break;
            
            // Skip byte rate (4 bytes) and block align (2 bytes)
            file.seek(file.position() + 6);
            
            if (file.read((uint8_t*)&bitsPerSample, 2) != 2) break;
            
            fmtFound = true;
            
            // Skip any remaining fmt chunk data (e.g., extended format info)
            uint32_t bytesRead = 16; // We've read 16 bytes of the fmt chunk
            if (chunk_size > bytesRead) {
                file.seek(file.position() + (chunk_size - bytesRead));
            }
            
            // Handle odd-sized chunks (must be word-aligned)
            if (chunk_size % 2 != 0) {
                file.seek(file.position() + 1);
            }
            
        } else if (strncmp(chunk_id, "data", 4) == 0) {
            // Found data chunk
            dataChunkPos = file.position(); // Position right after chunk header
            dataChunkSize = chunk_size;
            dataFound = true;
            
            // Don't read the data yet - we might need to find fmt first
            // Just skip past it
            file.seek(file.position() + chunk_size);
            if (chunk_size % 2 != 0) {
                file.seek(file.position() + 1);
            }
            
        } else {
            // Unknown chunk, skip it
            file.seek(file.position() + chunk_size);
            if (chunk_size % 2 != 0) {
                file.seek(file.position() + 1);
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
        return 0;
    }
    
    if (!dataFound) {
        logError("WAV file: 'data' chunk not found");
        return 0;
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
        return 0;
    }
    
    if (bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 32) {
        Serial.print("FIR Error: Unsupported bit depth: ");
        Serial.println(bitsPerSample);
        return 0;
    }
    
    if (audioFormat == 3 && bitsPerSample != 32) {
        logError("IEEE Float format must be 32-bit");
        return 0;
    }

    if (numChannels == 0) {
        logError("Invalid number of channels (0)");
        return 0;
    }

    // Warn if not mono
    if (numChannels != 1) {
        Serial.print("FIR Info: WAV file has ");
        Serial.print(numChannels);
        Serial.println(" channels. Using first channel only.");
    }

    // Calculate bytes per sample (for all channels)
    uint8_t bytesPerSample = bitsPerSample / 8;

    // Now read the actual data
    file.seek(dataChunkPos);
    
    int coeffCount = 0;
    uint32_t bytesProcessed = 0;

    // Read samples
    while (bytesProcessed < dataChunkSize && coeffCount < maxTaps) {
        float sample = 0.0f;
        bool readSuccess = false;

        // Read first channel's sample based on format
        if (audioFormat == 3 && bitsPerSample == 32) {
            // IEEE Float 32-bit
            float rawSample;
            if (file.read((uint8_t*)&rawSample, 4) == 4) {
                sample = rawSample;
                readSuccess = true;
                bytesProcessed += 4;
            }
            
        } else if (audioFormat == 1 && bitsPerSample == 16) {
            // PCM 16-bit signed
            int16_t rawSample;
            if (file.read((uint8_t*)&rawSample, 2) == 2) {
                // Normalize to -1.0 to +1.0
                sample = (float)rawSample / 32768.0f;
                readSuccess = true;
                bytesProcessed += 2;
            }
            
        } else if (audioFormat == 1 && bitsPerSample == 8) {
            // PCM 8-bit unsigned (offset by 128)
            uint8_t rawSample;
            if (file.read(&rawSample, 1) == 1) {
                // Convert from unsigned (0-255) to signed (-128 to +127), then normalize
                sample = ((float)rawSample - 128.0f) / 128.0f;
                readSuccess = true;
                bytesProcessed += 1;
            }
            
        } else if (audioFormat == 1 && bitsPerSample == 32) {
            // PCM 32-bit signed (less common, but supported by some tools)
            int32_t rawSample;
            if (file.read((uint8_t*)&rawSample, 4) == 4) {
                // Normalize to -1.0 to +1.0
                sample = (float)rawSample / 2147483648.0f;
                readSuccess = true;
                bytesProcessed += 4;
            }
        }

        if (!readSuccess) {
            logError("Failed to read sample data");
            return 0;
        }

        // Store the coefficient
        coeffs[coeffCount] = sample;
        coeffCount++;

        // Skip remaining channels if multi-channel
        if (numChannels > 1) {
            uint32_t skipBytes = (numChannels - 1) * bytesPerSample;
            
            // Safety check to avoid seeking past end of data chunk
            if (bytesProcessed + skipBytes > dataChunkSize) {
                logInfo("Reached end of data chunk while skipping channels");
                break;
            }
            
            file.seek(file.position() + skipBytes);
            bytesProcessed += skipBytes;
        }
    }

    // Report if we hit the tap limit
    if (bytesProcessed < dataChunkSize && coeffCount >= maxTaps) {
        Serial.print("FIR Info: WAV file contains more samples than max taps (");
        Serial.print(maxTaps);
        Serial.println("). Loaded first samples only.");
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
