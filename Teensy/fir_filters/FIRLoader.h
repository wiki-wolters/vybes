#ifndef FIR_LOADER_H
#define FIR_LOADER_H

#include <Arduino.h>
#include <SD.h>      // For File class and SD operations
#include "AudioFilterFIRFloat.h" // Include your new float FIR filter class

// Forward declaration of specific Teensy Audio FIR filter (if you still use it)
// This is not strictly needed if you only use AudioFilterFIRFloat now.
// #include <Audio.h> // If AudioFilterFIR is still used directly somewhere else

class FIRLoader {
public:
    // This method loads coefficients into a float array and returns a pointer to it.
    // The caller is responsible for deleting the returned float array.
    // The method will read the entire file to determine the number of taps.
    // Returns a pointer to the coefficients array and sets actualTaps to the number of taps found.
    static float* loadCoefficients(String filename, uint16_t& actualTaps, uint16_t maxTaps = 0);

private:
    // Helper methods, now static as they don't depend on FIRLoader instance state.
    static int loadFromTXT(File& file, float* coeffs, int maxTaps);
    static int loadFromWAV(File& file, float* coeffs, int maxTaps);
    static void normalizeCoefficients(float* coeffs, uint16_t numTaps);
    static bool isValidWAVHeader(const char* header);
    static void logError(String message);
    static void logInfo(String message);
};

#endif // FIR_LOADER_H