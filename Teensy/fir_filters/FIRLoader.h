#ifndef FIR_LOADER_H
#define FIR_LOADER_H

#include <Arduino.h>
#include "CoeffSource.h"
#ifndef VYBES_NATIVE
#include <SD.h>      // For File class and SD operations
#endif

class FIRLoader {
public:
#ifndef VYBES_NATIVE
    // This method loads coefficients into a float array and returns a pointer to it.
    // The caller is responsible for deleting the returned float array.
    // The method will read the entire file to determine the number of taps.
    // Returns a pointer to the coefficients array and sets actualTaps to the number of taps found.
    static float* loadCoefficients(String filename, uint16_t& actualTaps, uint16_t maxTaps = 0);
#endif

    // Core parse/load logic, operating on an abstract byte source so it can
    // be exercised host-side with in-memory fixtures. 'filename' is only
    // used for format detection (extension) and log messages.
    static float* loadCoefficients(CoeffSource& src, const String& filename,
                                   uint16_t& actualTaps, uint16_t maxTaps = 0);

private:
    // Helper methods, now static as they don't depend on FIRLoader instance state.
    static int loadFromTXT(CoeffSource& file, float* coeffs, int maxTaps);
    static int loadFromWAV(CoeffSource& file, float* coeffs, int maxTaps);
    static bool isValidWAVHeader(const char* header);
    static void logError(String message);
    static void logInfo(String message);
};

#endif // FIR_LOADER_H
