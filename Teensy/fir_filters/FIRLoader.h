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
    // This method now loads coefficients into a float array and returns a pointer to it.
    // The caller is responsible for deleting the returned float array.
    // It is no longer directly integrated with AudioFilterFIRFloat's begin method
    // because that object's constructor takes the pointer.
    // The 'taps' parameter is now 'maxTaps' to limit allocation.
    static float* loadCoefficients(String filename, uint16_t& actualTaps, int8_t maxTaps);

private:
    // Helper methods, now static as they don't depend on FIRLoader instance state.
    static int loadFromTXT(File& file, float* coeffs, int8_t maxTaps);
    static int loadFromWAV(File& file, float* coeffs, int8_t maxTaps);
    static bool isValidWAVHeader(const char* header);
    static void logError(String message);
    static void logInfo(String message);
};

#endif // FIR_LOADER_H