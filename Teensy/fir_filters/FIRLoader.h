#ifndef FIR_LOADER_H
#define FIR_LOADER_H

#include <Arduino.h>
#include <SD.h>
#include <Audio.h>

class FIRLoader {
public:
    // Main function to load a single FIR filter
    static bool loadFilter(String filename, AudioFilterFIR* filter, int8_t taps);
    
    // Helper function to load FIR coefficients into an array
    static int loadCoefficients(String filename, int16_t* coeffs, int8_t maxTaps);

private:
    // Format-specific loading functions
    static int loadFromTXT(File& file, int16_t* coeffs, int8_t maxTaps);
    static int loadFromWAV(File& file, int16_t* coeffs, int8_t maxTaps);
    
    // Utility functions
    static bool isValidWAVHeader(const char* header);
    static void logError(String message);
    static void logInfo(String message);
};

#endif // FIR_LOADER_H