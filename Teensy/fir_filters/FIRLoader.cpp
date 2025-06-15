#include "FIRLoader.h"

bool FIRLoader::loadFilter(String filename, AudioFilterFIR* filter, int8_t taps) {
    if (filename == "" || !filter) {
        return false;
    }
    
    // Allocate coefficient array
    int16_t* coeffs = new int16_t[taps];
    if (!coeffs) {
        logError("Failed to allocate memory for FIR coefficients");
        return false;
    }
    
    // Load coefficients from file and get actual count loaded
    int actualTaps = loadCoefficients(filename, coeffs, taps);
    
    if (actualTaps > 0) {
        filter->begin(coeffs, actualTaps);
        logInfo("Successfully loaded FIR filter: " + filename + " (using " + String(actualTaps) + " taps)");
    }
    
    delete[] coeffs;
    return actualTaps > 0;
}

int FIRLoader::loadCoefficients(String filename, int16_t* coeffs, int8_t taps) {
    File file = SD.open(filename.c_str());
    if (!file) {
        logError("Failed to open FIR file: " + filename);
        return 0;
    }
    
    int actualCoeffs = 0;
    
    // Determine file format and load accordingly
    if (filename.endsWith(".wav") || filename.endsWith(".WAV")) {
        actualCoeffs = loadFromWAV(file, coeffs, taps);
    } else if (filename.endsWith(".txt") || filename.endsWith(".TXT")) {
        actualCoeffs = loadFromTXT(file, coeffs, taps);
    } else {
        logError("Unsupported FIR file format: " + filename);
    }
    
    file.close();
    return actualCoeffs;
}

int FIRLoader::loadFromTXT(File& file, int16_t* coeffs, int8_t maxTaps) {
    int coeffCount = 0;
    String line = "";
    
    while (file.available() && coeffCount < maxTaps) {
        char c = file.read();
        
        if (c == '\n' || c == '\r' || c == ',' || c == ' ' || c == '\t') {
            if (line.length() > 0) {
                float floatVal = line.toFloat();
                coeffs[coeffCount] = (int16_t)(floatVal * 32767.0f);
                coeffCount++;
                line = "";
            }
        } else if (c != '\0') {
            line += c;
        }
    }
    
    // Handle last coefficient
    if (line.length() > 0 && coeffCount < maxTaps) {
        float floatVal = line.toFloat();
        coeffs[coeffCount] = (int16_t)(floatVal * 32767.0f);
        coeffCount++;
    }
    
    // Report if file had more coefficients than we could use
    if (file.available()) {
        logInfo("File has more coefficients than requested taps (" + String(maxTaps) + "), using first " + String(coeffCount));
    }
    
    return coeffCount;
}

int FIRLoader::loadFromWAV(File& file, int16_t* coeffs, int8_t maxTaps) {
    if (file.size() < 44) {
        logError("WAV file too small");
        return 0;
    }
    
    // Read WAV header
    file.seek(0);
    char header[44];
    file.read(header, 44);
    
    if (!isValidWAVHeader(header)) {
        logError("Invalid WAV file format");
        return 0;
    }
    
    // Extract format info
    uint16_t audioFormat = *(uint16_t*)(header + 20);
    uint16_t numChannels = *(uint16_t*)(header + 22);
    uint16_t bitsPerSample = *(uint16_t*)(header + 34);
    
    if (audioFormat != 1) {
        logError("Only PCM WAV files supported");
        return 0;
    }
    
    uint8_t bytesPerSample = bitsPerSample / 8;
    int coeffCount = 0;
    
    // Read audio data
    while (file.available() && coeffCount < maxTaps) {
        if (bitsPerSample == 16) {
            int16_t sample;
            if (file.read((uint8_t*)&sample, 2) == 2) {
                coeffs[coeffCount] = sample;
                coeffCount++;
                
                if (numChannels > 1) {
                    file.seek(file.position() + (numChannels - 1) * bytesPerSample);
                }
            }
        } else if (bitsPerSample == 8) {
            uint8_t sample;
            if (file.read(&sample, 1) == 1) {
                coeffs[coeffCount] = (int16_t)((sample - 128) * 256);
                coeffCount++;
                
                if (numChannels > 1) {
                    file.seek(file.position() + (numChannels - 1));
                }
            }
        } else {
            logError("Unsupported bit depth: " + String(bitsPerSample));
            return 0;
        }
    }
    
    // Report if file had more samples than we could use
    if (file.available()) {
        logInfo("WAV file has more samples than requested taps (" + String(maxTaps) + "), using first " + String(coeffCount));
    }
    
    return coeffCount;
}

bool FIRLoader::isValidWAVHeader(const char* header) {
    return (strncmp(header, "RIFF", 4) == 0 && strncmp(header + 8, "WAVE", 4) == 0);
}

void FIRLoader::logError(String message) {
    Serial.println("FIR Error: " + message);
}

void FIRLoader::logInfo(String message) {
    Serial.println("FIR Info: " + message);
}