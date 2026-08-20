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
    // Adapter exposing an open SD File through the CoeffSource interface.
    // The caller owns the File and must keep it open for the adapter's life.
    class FileSource : public CoeffSource {
    public:
        explicit FileSource(File& f) : f(f) {}
        int read(void* buf, size_t len) override { return f.read(buf, len); }
        int read() override { return f.read(); }
        bool seek(uint64_t pos) override { return f.seek(pos); }
        uint64_t position() override { return f.position(); }
        int available() override { return f.available(); }
        uint64_t size() override { return f.size(); }
    private:
        File& f;
    };

    // Exact tap count of an open WAV file (0 if the header can't be parsed).
    // Reads only the header chunks - the data chunk is never read - so it is
    // cheap enough to call per file while building the SD listing.
    static long countWavTaps(File& file);

    // Exact tap count of an open TXT file. One streamed pass over the whole
    // file; coefficient files are tens of KB, so this too is cheap enough to
    // call per file while building the SD listing.
    static long countTxtTaps(File& file);
#endif

    // Streaming coefficient reader, and the one parse path for every
    // supported format. Counting is separated from reading so a caller can
    // decide whether a file fits the shared tap pool before a byte of filter
    // data is touched, and the coefficients themselves are handed out in
    // order through CoeffFeed - FirEngine::buildPending consumes them one
    // partition at a time, so a filter-sized array never has to exist.
    class Stream : public CoeffFeed {
    public:
        // Count the coefficients in src (WAV: header chunks only; TXT: one
        // tokenizing pass; BIN: file size). Returns the count, or 0 if the
        // file can't be used at all - unsupported extension, unparseable or
        // truncated header, no coefficients. Reads no coefficient data.
        long begin(CoeffSource& src, const String& filename);

        // Parse and validate the sample format, and position the read cursor
        // at the first coefficient. Returns false for a format the loader
        // can't convert. Call once, after begin() and after the tap count has
        // been accepted, since it is the point of no return for the cursor.
        bool prepare();

        // CoeffFeed: the next coefficients, in file order.
        uint16_t read(float* dst, uint16_t count) override;

        // True once a read came up short - a truncated or unreadable file.
        // Callers must not report that as an allocation failure.
        bool starved() const { return starvedFlag; }

    private:
        enum Format { FMT_NONE, FMT_WAV, FMT_TXT, FMT_BIN };

        bool prepareWav();
        uint16_t readWav(float* dst, uint16_t count);
        uint16_t readTxt(float* dst, uint16_t count);
        uint16_t readBin(float* dst, uint16_t count);

        CoeffSource* src = nullptr;
        Format format = FMT_NONE;
        long count = 0;
        bool prepared = false;
        bool starvedFlag = false;

        // WAV state, resolved by prepareWav()
        uint16_t audioFormat = 0;
        uint16_t numChannels = 0;
        uint16_t bitsPerSample = 0;
        uint32_t dataSize = 0;
        uint32_t bytesProcessed = 0;

        // TXT state: a coefficient token can straddle two read() calls
        String token;
    };

    // Core WAV tap counter: parses the chunk list for 'fmt ' (bit depth,
    // channels) and 'data' (size) and returns the exact number of frames.
    // Metadata chunks (fact, LIST/INFO, ...) are skipped, so unlike any
    // file-size heuristic this never over- or undercounts. Returns 0 if the
    // header can't be parsed. 'filename' is only used for log messages.
    static long countWavTaps(CoeffSource& src, const String& filename);

    // Core TXT tap counter: counts coefficient tokens with the same
    // delimiter set the TXT reader parses by (\n \r , space tab; NULs
    // ignored), so the count always matches what a load of the file would
    // produce. Bytes-per-tap varies with the exporting tool (rePhase
    // averages ~23 bytes per coefficient line), so unlike a file-size
    // heuristic this never over- or undercounts. Stream::begin uses it too -
    // the SD listing and the loader can't drift apart.
    static long countTxtTaps(CoeffSource& src);

    // Load a whole file into a freshly allocated array (caller deletes it).
    // Convenient for tests and one-shot uses, but it holds the filter in RAM
    // twice while the engine builds - use Stream for anything loaded into a
    // filter. 'filename' is only used for format detection (extension) and
    // log messages.
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
    static void logError(String message);
    static void logInfo(String message);
};

#endif // FIR_LOADER_H
