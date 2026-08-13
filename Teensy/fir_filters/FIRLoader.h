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
    static float* loadCoefficients(String filename, uint16_t& actualTaps, uint16_t maxTaps = 0,
                                   bool truncateToMax = true);

    // Exact tap count of an open WAV file (0 if the header can't be parsed).
    // Reads only the header chunks - the data chunk is never read - so it is
    // cheap enough to call per file while building the SD listing.
    static long countWavTaps(File& file);

    // Exact tap count of an open TXT file. One streamed pass over the whole
    // file; coefficient files are tens of KB, so this too is cheap enough to
    // call per file while building the SD listing.
    static long countTxtTaps(File& file);
#endif

    // Core WAV tap counter: parses the chunk list for 'fmt ' (bit depth,
    // channels) and 'data' (size) and returns the exact number of frames.
    // Metadata chunks (fact, LIST/INFO, ...) are skipped, so unlike any
    // file-size heuristic this never over- or undercounts. Returns 0 if the
    // header can't be parsed. 'filename' is only used for log messages.
    static long countWavTaps(CoeffSource& src, const String& filename);

    // Core TXT tap counter: counts coefficient tokens with the same
    // delimiter set loadFromTXT parses by (\n \r , space tab; NULs ignored),
    // so the count always matches what a load of the file would produce.
    // Bytes-per-tap varies with the exporting tool (rePhase averages ~23
    // bytes per coefficient line), so unlike a file-size heuristic this
    // never over- or undercounts. loadCoefficients uses it too - the SD
    // listing and the loader can't drift apart.
    static long countTxtTaps(CoeffSource& src);

    // Core parse/load logic, operating on an abstract byte source so it can
    // be exercised host-side with in-memory fixtures. 'filename' is only
    // used for format detection (extension) and log messages.
    //
    // A file with more than maxTaps coefficients is truncated by default;
    // with truncateToMax = false the load is rejected instead (returns
    // nullptr with actualTaps set to the file's tap count, capped to
    // uint16_t range, so the caller can report how much was asked for -
    // this is how the shared FIR tap pool refuses oversized loads).
    static float* loadCoefficients(CoeffSource& src, const String& filename,
                                   uint16_t& actualTaps, uint16_t maxTaps = 0,
                                   bool truncateToMax = true);

private:
    // Helper methods, now static as they don't depend on FIRLoader instance state.
    static int loadFromTXT(CoeffSource& file, float* coeffs, int maxTaps);
    static int loadFromWAV(CoeffSource& file, float* coeffs, int maxTaps);
    static int loadFromBIN(CoeffSource& file, float* coeffs, int maxTaps);
    static bool isValidWAVHeader(const char* header);
    static void logError(String message);
    static void logInfo(String message);
};

#endif // FIR_LOADER_H
