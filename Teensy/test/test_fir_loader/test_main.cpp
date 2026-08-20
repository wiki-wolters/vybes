// FIRLoader parsing tests: in-memory WAV/TXT fixtures fed through the
// CoeffSource abstraction. Valid files must load the exact coefficients
// (verbatim - no normalization, no scaling); malformed files must fail
// cleanly (nullptr, 0 taps, no crash).

#include <unity.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "FIRLoader.h"

// --- In-memory CoeffSource with SD File semantics ---
class MemorySource : public CoeffSource {
public:
    explicit MemorySource(std::vector<uint8_t> data) : d(std::move(data)) {}
    explicit MemorySource(const std::string& text) : d(text.begin(), text.end()) {}

    int read(void* buf, size_t len) override {
        size_t n = d.size() - pos;
        if (len < n) n = len;
        memcpy(buf, d.data() + pos, n);
        pos += n;
        return (int)n;
    }
    int read() override { return pos < d.size() ? d[pos++] : -1; }
    bool seek(uint64_t p) override {
        if (p > d.size()) return false; // like File: can't seek past EOF
        pos = (size_t)p;
        return true;
    }
    uint64_t position() override { return pos; }
    int available() override { return (int)(d.size() - pos); }
    uint64_t size() override { return d.size(); }

private:
    std::vector<uint8_t> d;
    size_t pos = 0;
};

// --- Little-endian WAV fixture builder ---
static void put16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(x & 0xFF);
    v.push_back((x >> 8) & 0xFF);
}
static void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x & 0xFF);
    v.push_back((x >> 8) & 0xFF);
    v.push_back((x >> 16) & 0xFF);
    v.push_back((x >> 24) & 0xFF);
}
static void putFloat(std::vector<uint8_t>& v, float f) {
    uint32_t bits;
    memcpy(&bits, &f, 4);
    put32(v, bits);
}
static void putTag(std::vector<uint8_t>& v, const char* tag) {
    v.insert(v.end(), tag, tag + 4);
}

struct WavOptions {
    uint16_t format = 3;        // 3 = IEEE float, 1 = PCM
    uint16_t channels = 1;
    uint16_t bitsPerSample = 32;
    bool junkChunkBeforeFmt = false; // odd-sized chunk + pad byte
    bool factChunkAfterFmt = false;  // 'fact' chunk, as float WAV encoders emit
    int64_t dataSizeOverride = -1;   // declared 'data' size, if != actual
};

// Builds RIFF/WAVE with optional junk chunk, fmt, optional fact, then data.
static std::vector<uint8_t> buildWav(const std::vector<uint8_t>& dataBytes,
                                     const WavOptions& opt = WavOptions()) {
    std::vector<uint8_t> body; // everything after "RIFF"+size ("WAVE"...)
    putTag(body, "WAVE");

    if (opt.junkChunkBeforeFmt) {
        putTag(body, "junk");
        put32(body, 3);           // odd chunk size
        body.push_back('x');
        body.push_back('y');
        body.push_back('z');
        body.push_back(0);        // padding byte to even boundary
    }

    putTag(body, "fmt ");
    put32(body, 16);
    put16(body, opt.format);
    put16(body, opt.channels);
    put32(body, 44100);           // sample rate
    uint32_t byteRate = 44100u * opt.channels * (opt.bitsPerSample / 8);
    put32(body, byteRate);
    put16(body, (uint16_t)(opt.channels * (opt.bitsPerSample / 8)));
    put16(body, opt.bitsPerSample);

    if (opt.factChunkAfterFmt) {
        putTag(body, "fact");
        put32(body, 4);           // dwSampleLength: frames per channel
        put32(body, (uint32_t)(dataBytes.size() / (opt.bitsPerSample / 8) / opt.channels));
    }

    putTag(body, "data");
    uint32_t declared = (opt.dataSizeOverride >= 0) ? (uint32_t)opt.dataSizeOverride
                                                    : (uint32_t)dataBytes.size();
    put32(body, declared);
    body.insert(body.end(), dataBytes.begin(), dataBytes.end());

    std::vector<uint8_t> wav;
    putTag(wav, "RIFF");
    put32(wav, (uint32_t)body.size());
    wav.insert(wav.end(), body.begin(), body.end());
    return wav;
}

// Convenience: load through the FIRLoader core, returning a managed pointer
static float* load(std::vector<uint8_t> bytes, const char* name,
                   uint16_t& taps, uint16_t maxTaps = 0) {
    MemorySource src(std::move(bytes));
    return FIRLoader::loadCoefficients(src, String(name), taps, maxTaps);
}

// --- WAV tests ---

static void test_valid_float32_wav_loads_verbatim(void) {
    // Includes values a normalizer would rescale (peak > 1) and a negative
    const float expected[] = {0.5f, -0.25f, 2.0f, 0.125f, -1.5f, 1e-6f};
    std::vector<uint8_t> data;
    for (float f : expected) putFloat(data, f);

    uint16_t taps = 0;
    float* coeffs = load(buildWav(data), "filter.wav", taps);
    TEST_ASSERT_NOT_NULL(coeffs);
    TEST_ASSERT_EQUAL_UINT16(6, taps);
    // Bit-exact: verbatim load, no normalization or 0.5 scaling
    TEST_ASSERT_EQUAL_MEMORY(expected, coeffs, sizeof(expected));
    delete[] coeffs;
}

static void test_single_unity_coefficient_stays_unity(void) {
    std::vector<uint8_t> data;
    putFloat(data, 1.0f);
    uint16_t taps = 0;
    float* coeffs = load(buildWav(data), "dirac.wav", taps);
    TEST_ASSERT_NOT_NULL(coeffs);
    TEST_ASSERT_EQUAL_UINT16(1, taps);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, coeffs[0]); // not 0.5
    delete[] coeffs;
}

static void test_wav_with_odd_sized_chunk_and_padding(void) {
    const float expected[] = {0.25f, -0.75f, 0.5f};
    std::vector<uint8_t> data;
    for (float f : expected) putFloat(data, f);
    WavOptions opt;
    opt.junkChunkBeforeFmt = true;
    uint16_t taps = 0;
    float* coeffs = load(buildWav(data, opt), "odd.wav", taps);
    TEST_ASSERT_NOT_NULL(coeffs);
    TEST_ASSERT_EQUAL_UINT16(3, taps);
    TEST_ASSERT_EQUAL_MEMORY(expected, coeffs, sizeof(expected));
    delete[] coeffs;
}

static void test_multichannel_wav_takes_first_channel(void) {
    // Stereo: frames (L,R) - expect the L samples only
    const float left[] = {0.1f, 0.2f, 0.3f, 0.4f};
    const float right[] = {-9.0f, -9.0f, -9.0f, -9.0f};
    std::vector<uint8_t> data;
    for (int i = 0; i < 4; i++) {
        putFloat(data, left[i]);
        putFloat(data, right[i]);
    }
    WavOptions opt;
    opt.channels = 2;
    uint16_t taps = 0;
    float* coeffs = load(buildWav(data, opt), "stereo.wav", taps);
    TEST_ASSERT_NOT_NULL(coeffs);
    TEST_ASSERT_EQUAL_UINT16(4, taps); // frames, not raw samples
    TEST_ASSERT_EQUAL_MEMORY(left, coeffs, sizeof(left));
    delete[] coeffs;
}

static void test_pcm16_wav_normalizes_samples(void) {
    std::vector<uint8_t> data;
    put16(data, 16384);           // 0.5
    put16(data, (uint16_t)-32768); // -1.0
    WavOptions opt;
    opt.format = 1;
    opt.bitsPerSample = 16;
    uint16_t taps = 0;
    float* coeffs = load(buildWav(data, opt), "pcm.wav", taps);
    TEST_ASSERT_NOT_NULL(coeffs);
    TEST_ASSERT_EQUAL_UINT16(2, taps);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, coeffs[0]);
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, coeffs[1]);
    delete[] coeffs;
}

static void test_truncated_wav_fails_cleanly(void) {
    // Declares 16 floats in the data chunk but only carries 4
    std::vector<uint8_t> data;
    for (int i = 0; i < 4; i++) putFloat(data, 0.5f);
    WavOptions opt;
    opt.dataSizeOverride = 16 * 4;
    uint16_t taps = 123;
    float* coeffs = load(buildWav(data, opt), "trunc.wav", taps);
    TEST_ASSERT_NULL(coeffs);
    TEST_ASSERT_EQUAL_UINT16(0, taps);
}

static void test_garbage_bytes_fail_cleanly(void) {
    std::vector<uint8_t> garbage;
    for (int i = 0; i < 300; i++) garbage.push_back((uint8_t)(i * 37 + 11));
    uint16_t taps = 123;
    float* coeffs = load(garbage, "garbage.wav", taps);
    TEST_ASSERT_NULL(coeffs);
    TEST_ASSERT_EQUAL_UINT16(0, taps);
}

static void test_tiny_wav_fails_cleanly(void) {
    std::vector<uint8_t> tiny = {'R', 'I', 'F', 'F', 4, 0, 0, 0, 'W', 'A', 'V', 'E'};
    uint16_t taps = 123;
    float* coeffs = load(tiny, "tiny.wav", taps);
    TEST_ASSERT_NULL(coeffs);
    TEST_ASSERT_EQUAL_UINT16(0, taps);
}

static void test_wav_missing_data_chunk_fails_cleanly(void) {
    std::vector<uint8_t> body;
    putTag(body, "WAVE");
    putTag(body, "fmt ");
    put32(body, 16);
    put16(body, 3);
    put16(body, 1);
    put32(body, 44100);
    put32(body, 44100 * 4);
    put16(body, 4);
    put16(body, 32);
    std::vector<uint8_t> wav;
    putTag(wav, "RIFF");
    put32(wav, (uint32_t)body.size());
    wav.insert(wav.end(), body.begin(), body.end());
    // Pad to >= 44 bytes so it isn't rejected for size alone
    while (wav.size() < 44) wav.push_back(0);

    uint16_t taps = 123;
    float* coeffs = load(wav, "nodata.wav", taps);
    TEST_ASSERT_NULL(coeffs);
    TEST_ASSERT_EQUAL_UINT16(0, taps);
}

// --- countWavTaps tests (the exact counts the SD listing reports, which
// the ESP's tap-pool accounting trusts verbatim - so they must never
// overcount: an exact-fit pool config has zero headroom) ---

static long countTaps(std::vector<uint8_t> bytes, const char* name) {
    MemorySource src(std::move(bytes));
    return FIRLoader::countWavTaps(src, String(name));
}

static void test_count_taps_plain_float32_wav(void) {
    // 6144 mono float samples behind the minimal 44-byte header: the
    // exact-fit case (3072+3072+6144 fills the 12288 pool exactly)
    std::vector<uint8_t> data;
    for (int i = 0; i < 6144; i++) putFloat(data, 0.5f);
    TEST_ASSERT_EQUAL_INT32(6144, countTaps(buildWav(data), "fit.wav"));
}

static void test_count_taps_float32_wav_with_fact_chunk(void) {
    // Float WAV encoders commonly add a 'fact' chunk; a fixed-header
    // (size-44)/4 heuristic would overcount this file by 3 taps
    std::vector<uint8_t> data;
    for (int i = 0; i < 6144; i++) putFloat(data, 0.5f);
    WavOptions opt;
    opt.factChunkAfterFmt = true;
    TEST_ASSERT_EQUAL_INT32(6144, countTaps(buildWav(data, opt), "fact.wav"));
}

static void test_count_taps_with_junk_and_fact_chunks(void) {
    std::vector<uint8_t> data;
    for (int i = 0; i < 100; i++) putFloat(data, 0.25f);
    WavOptions opt;
    opt.junkChunkBeforeFmt = true;
    opt.factChunkAfterFmt = true;
    TEST_ASSERT_EQUAL_INT32(100, countTaps(buildWav(data, opt), "meta.wav"));
}

static void test_count_taps_pcm16_wav(void) {
    std::vector<uint8_t> data;
    for (int i = 0; i < 48; i++) put16(data, (uint16_t)i);
    WavOptions opt;
    opt.format = 1;
    opt.bitsPerSample = 16;
    TEST_ASSERT_EQUAL_INT32(48, countTaps(buildWav(data, opt), "pcm.wav"));
}

static void test_count_taps_stereo_wav_counts_frames(void) {
    std::vector<uint8_t> data;
    for (int i = 0; i < 32; i++) putFloat(data, 0.1f); // 16 stereo frames
    WavOptions opt;
    opt.channels = 2;
    TEST_ASSERT_EQUAL_INT32(16, countTaps(buildWav(data, opt), "stereo.wav"));
}

static void test_count_taps_garbage_is_zero(void) {
    std::vector<uint8_t> garbage;
    for (int i = 0; i < 300; i++) garbage.push_back((uint8_t)(i * 37 + 11));
    TEST_ASSERT_EQUAL_INT32(0, countTaps(garbage, "garbage.wav"));
}

static void test_count_taps_sub_byte_bit_depth_is_zero(void) {
    // A bit depth below 8 bytes-per-sample divides to zero: the counter must
    // report "unknown" rather than divide by it
    std::vector<uint8_t> data(64, 0);
    WavOptions opt;
    opt.format = 1;
    opt.bitsPerSample = 4;
    TEST_ASSERT_EQUAL_INT32(0, countTaps(buildWav(data, opt), "nibble.wav"));
}

static void test_count_taps_oversized_data_chunk_is_zero(void) {
    // A 'data' size past the end of the file is corrupt or truncated. Counting
    // from it would claim ~1e9 taps, which then drives the shared pool
    // accounting and the load allocation.
    std::vector<uint8_t> data;
    for (int i = 0; i < 64; i++) putFloat(data, 0.5f);
    WavOptions opt;
    opt.dataSizeOverride = 0xFFFFFF00;
    TEST_ASSERT_EQUAL_INT32(0, countTaps(buildWav(data, opt), "huge.wav"));
}

// --- TXT tests ---

static void test_valid_txt_loads_exact_coefficients(void) {
    uint16_t taps = 0;
    MemorySource src(std::string("0.5,-0.25\n1.0e-3 2\t7\n"));
    float* coeffs = FIRLoader::loadCoefficients(src, String("coeffs.txt"), taps);
    TEST_ASSERT_NOT_NULL(coeffs);
    TEST_ASSERT_EQUAL_UINT16(5, taps);
    const float expected[] = {0.5f, -0.25f, 1.0e-3f, 2.0f, 7.0f};
    for (int i = 0; i < 5; i++) TEST_ASSERT_EQUAL_FLOAT(expected[i], coeffs[i]);
    delete[] coeffs;
}

static void test_txt_without_trailing_newline(void) {
    uint16_t taps = 0;
    MemorySource src(std::string("0.125 -0.5"));
    float* coeffs = FIRLoader::loadCoefficients(src, String("tail.txt"), taps);
    TEST_ASSERT_NOT_NULL(coeffs);
    TEST_ASSERT_EQUAL_UINT16(2, taps);
    TEST_ASSERT_EQUAL_FLOAT(0.125f, coeffs[0]);
    TEST_ASSERT_EQUAL_FLOAT(-0.5f, coeffs[1]);
    delete[] coeffs;
}

static void test_empty_txt_fails_cleanly(void) {
    uint16_t taps = 123;
    MemorySource src(std::string("  \n\t \n"));
    float* coeffs = FIRLoader::loadCoefficients(src, String("empty.txt"), taps);
    TEST_ASSERT_NULL(coeffs);
    TEST_ASSERT_EQUAL_UINT16(0, taps);
}

// --- countTxtTaps tests (the exact counts the SD listing reports for text
// files; like the WAV counts, the ESP's tap-pool accounting trusts them
// verbatim, so they must match what a load would parse - and never
// overcount) ---

static long countTxt(const std::string& text) {
    MemorySource src(text);
    return FIRLoader::countTxtTaps(src);
}

static void test_count_txt_rephase_style_lines(void) {
    // rePhase exports one full-precision coefficient per line, ~23 bytes
    // each - a size/12 heuristic would claim ~6100 taps for these 3072
    // and wrongly reject a config that actually fits the pool
    std::string text;
    for (int i = 0; i < 3072; i++) text += "-1.2045678901234567e-05\n";
    TEST_ASSERT_EQUAL_INT32(3072, countTxt(text));
}

static void test_count_txt_matches_loader_tokenization(void) {
    // Mixed delimiters, run-together delimiters, no trailing newline:
    // the count must equal the taps a load of the same bytes reports
    std::string text("0.5,-0.25\n\n1.0e-3  2\t\t7\r\n0.125");
    long counted = countTxt(text);
    TEST_ASSERT_EQUAL_INT32(6, counted);

    uint16_t taps = 0;
    MemorySource src(text);
    float* coeffs = FIRLoader::loadCoefficients(src, String("mixed.txt"), taps);
    TEST_ASSERT_NOT_NULL(coeffs);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)counted, taps);
    delete[] coeffs;
}

static void test_count_txt_token_at_eof_counts(void) {
    TEST_ASSERT_EQUAL_INT32(2, countTxt("0.125 -0.5"));
}

static void test_count_txt_whitespace_only_is_zero(void) {
    TEST_ASSERT_EQUAL_INT32(0, countTxt("  \n\t \r\n,"));
    TEST_ASSERT_EQUAL_INT32(0, countTxt(""));
}

static void test_count_txt_nul_bytes_do_not_split_tokens(void) {
    // The loader skips NULs without ending the token; the counter must too
    std::string text("0.");
    text += '\0';
    text += "5 2.0\n";
    TEST_ASSERT_EQUAL_INT32(2, countTxt(text));
}

// --- Misc ---

static void test_unsupported_extension_fails_cleanly(void) {
    uint16_t taps = 123;
    MemorySource src(std::string("0.5 0.25"));
    float* coeffs = FIRLoader::loadCoefficients(src, String("coeffs.dat"), taps);
    TEST_ASSERT_NULL(coeffs);
    TEST_ASSERT_EQUAL_UINT16(0, taps);
}

// --- BIN tests (raw little-endian float32, no header) ---

static void test_valid_bin_loads_verbatim(void) {
    const float expected[] = {0.5f, -0.25f, 2.0f, -1.5f};
    std::vector<uint8_t> data;
    for (float f : expected) putFloat(data, f);

    uint16_t taps = 0;
    std::unique_ptr<float[]> coeffs(load(data, "impulse.bin", taps));
    TEST_ASSERT_NOT_NULL(coeffs.get());
    TEST_ASSERT_EQUAL_UINT16(4, taps);
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_FLOAT(expected[i], coeffs[i]);
}

static void test_empty_bin_fails_cleanly(void) {
    uint16_t taps = 123;
    float* coeffs = load(std::vector<uint8_t>(), "empty.bin", taps);
    TEST_ASSERT_NULL(coeffs);
    TEST_ASSERT_EQUAL_UINT16(0, taps);
}

// --- reject-over-limit (the shared tap pool's refusal path) ---

static void test_reject_over_limit_returns_requested_taps(void) {
    std::vector<uint8_t> data;
    for (int i = 0; i < 10; i++) putFloat(data, (float)i);
    uint16_t taps = 0;
    MemorySource src(buildWav(data));
    float* coeffs = FIRLoader::loadCoefficients(src, String("long.wav"), taps, 4,
                                                /*truncateToMax=*/false);
    TEST_ASSERT_NULL(coeffs);
    TEST_ASSERT_EQUAL_UINT16(10, taps); // reports what the file asked for
}

static void test_max_taps_limits_load(void) {
    std::vector<uint8_t> data;
    for (int i = 0; i < 10; i++) putFloat(data, (float)i);
    uint16_t taps = 0;
    float* coeffs = load(buildWav(data), "long.wav", taps, 4);
    TEST_ASSERT_NOT_NULL(coeffs);
    TEST_ASSERT_EQUAL_UINT16(4, taps);
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_FLOAT((float)i, coeffs[i]);
    delete[] coeffs;
}

// --- Stream: the incremental path the FIR engine loads through ---
// Same parse, same coefficients, but handed out a few at a time, so a
// partition-sized pull off the SD card never needs the filter in RAM.

// Drains a Stream in fixed-size bites and returns everything it produced.
static std::vector<float> drain(FIRLoader::Stream& stream, long taps, uint16_t bite) {
    std::vector<float> out;
    std::vector<float> buf(bite);
    long left = taps;
    while (left > 0) {
        uint16_t want = (uint16_t)((left < (long)bite) ? left : (long)bite);
        uint16_t n = stream.read(buf.data(), want);
        out.insert(out.end(), buf.begin(), buf.begin() + n);
        if (n < want) break; // starved - let the caller assert on it
        left -= n;
    }
    return out;
}

// Every fixture must stream to exactly what a whole-file load produces, at
// any bite size - including bites that split WAV frames and TXT tokens.
static void assertStreamMatchesWholeFile(const std::vector<uint8_t>& bytes, const char* name) {
    uint16_t taps = 0;
    std::unique_ptr<float[]> whole(load(bytes, name, taps));
    TEST_ASSERT_NOT_NULL_MESSAGE(whole.get(), name);
    TEST_ASSERT_GREATER_THAN_UINT16(0, taps);

    for (uint16_t bite : {(uint16_t)1, (uint16_t)3, (uint16_t)taps}) {
        MemorySource src(bytes);
        FIRLoader::Stream stream;
        TEST_ASSERT_EQUAL_INT32(taps, stream.begin(src, String(name)));
        TEST_ASSERT_TRUE(stream.prepare());
        std::vector<float> streamed = drain(stream, taps, bite);
        TEST_ASSERT_FALSE(stream.starved());
        TEST_ASSERT_EQUAL_UINT32(taps, streamed.size());
        for (uint16_t i = 0; i < taps; i++) {
            TEST_ASSERT_EQUAL_FLOAT(whole[i], streamed[i]);
        }
    }
}

static void test_stream_float32_wav_matches_whole_file(void) {
    std::vector<uint8_t> data;
    for (int i = 0; i < 9; i++) putFloat(data, (float)i * 0.125f - 0.5f);
    assertStreamMatchesWholeFile(buildWav(data), "stream.wav");
}

static void test_stream_multichannel_wav_matches_whole_file(void) {
    std::vector<uint8_t> data;
    for (int i = 0; i < 7; i++) {
        putFloat(data, (float)i * 0.1f);
        putFloat(data, -9.0f); // right channel, must be skipped
    }
    WavOptions opt;
    opt.channels = 2;
    assertStreamMatchesWholeFile(buildWav(data, opt), "stereo-stream.wav");
}

static void test_stream_pcm16_wav_matches_whole_file(void) {
    std::vector<uint8_t> data;
    for (int i = 0; i < 6; i++) put16(data, (uint16_t)(int16_t)(i * 4096));
    WavOptions opt;
    opt.format = 1;
    opt.bitsPerSample = 16;
    assertStreamMatchesWholeFile(buildWav(data, opt), "pcm-stream.wav");
}

static void test_stream_bin_matches_whole_file(void) {
    std::vector<uint8_t> data;
    for (int i = 0; i < 5; i++) putFloat(data, (float)i - 2.0f);
    assertStreamMatchesWholeFile(data, "stream.bin");
}

// The one case a chunked reader can get wrong on its own: a coefficient's
// digits split across two read() calls.
static void test_stream_txt_token_split_across_bites(void) {
    std::string text = "0.125\n-0.25\n0.5\n0.03125\n1.5";
    std::vector<uint8_t> bytes(text.begin(), text.end());
    assertStreamMatchesWholeFile(bytes, "stream.txt");
}

static void test_stream_rejects_unknown_extension(void) {
    MemorySource src(std::string("0.5 0.25"));
    FIRLoader::Stream stream;
    TEST_ASSERT_EQUAL_INT32(0, stream.begin(src, String("coeffs.dat")));
    TEST_ASSERT_FALSE(stream.prepare());
    float dst[4];
    TEST_ASSERT_EQUAL_UINT16(0, stream.read(dst, 4));
}

// A count the header can give but the reader can't honour: prepare() is
// where that is caught, after the pool has already sized the file.
static void test_stream_prepare_rejects_unsupported_encoding(void) {
    std::vector<uint8_t> data;
    for (int i = 0; i < 4; i++) put16(data, (uint16_t)i);
    WavOptions opt;
    opt.format = 2; // neither PCM nor IEEE float
    opt.bitsPerSample = 16;
    std::vector<uint8_t> bytes = buildWav(data, opt);

    MemorySource src(bytes);
    FIRLoader::Stream stream;
    TEST_ASSERT_EQUAL_INT32(4, stream.begin(src, String("adpcm.wav")));
    TEST_ASSERT_FALSE(stream.prepare());
}

// Reading past the end flags starvation rather than inventing coefficients,
// so the caller can tell a broken file from an exhausted heap.
static void test_stream_read_past_end_starves(void) {
    std::vector<uint8_t> data;
    for (int i = 0; i < 3; i++) putFloat(data, (float)i);

    MemorySource src(data);
    FIRLoader::Stream stream;
    TEST_ASSERT_EQUAL_INT32(3, stream.begin(src, String("short.bin")));
    TEST_ASSERT_TRUE(stream.prepare());

    float dst[8];
    TEST_ASSERT_EQUAL_UINT16(3, stream.read(dst, 8));
    TEST_ASSERT_TRUE(stream.starved());
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_float32_wav_loads_verbatim);
    RUN_TEST(test_single_unity_coefficient_stays_unity);
    RUN_TEST(test_wav_with_odd_sized_chunk_and_padding);
    RUN_TEST(test_multichannel_wav_takes_first_channel);
    RUN_TEST(test_pcm16_wav_normalizes_samples);
    RUN_TEST(test_truncated_wav_fails_cleanly);
    RUN_TEST(test_garbage_bytes_fail_cleanly);
    RUN_TEST(test_tiny_wav_fails_cleanly);
    RUN_TEST(test_wav_missing_data_chunk_fails_cleanly);
    RUN_TEST(test_count_taps_plain_float32_wav);
    RUN_TEST(test_count_taps_float32_wav_with_fact_chunk);
    RUN_TEST(test_count_taps_with_junk_and_fact_chunks);
    RUN_TEST(test_count_taps_pcm16_wav);
    RUN_TEST(test_count_taps_stereo_wav_counts_frames);
    RUN_TEST(test_count_taps_garbage_is_zero);
    RUN_TEST(test_count_taps_sub_byte_bit_depth_is_zero);
    RUN_TEST(test_count_taps_oversized_data_chunk_is_zero);
    RUN_TEST(test_valid_txt_loads_exact_coefficients);
    RUN_TEST(test_txt_without_trailing_newline);
    RUN_TEST(test_empty_txt_fails_cleanly);
    RUN_TEST(test_count_txt_rephase_style_lines);
    RUN_TEST(test_count_txt_matches_loader_tokenization);
    RUN_TEST(test_count_txt_token_at_eof_counts);
    RUN_TEST(test_count_txt_whitespace_only_is_zero);
    RUN_TEST(test_count_txt_nul_bytes_do_not_split_tokens);
    RUN_TEST(test_unsupported_extension_fails_cleanly);
    RUN_TEST(test_valid_bin_loads_verbatim);
    RUN_TEST(test_empty_bin_fails_cleanly);
    RUN_TEST(test_max_taps_limits_load);
    RUN_TEST(test_reject_over_limit_returns_requested_taps);
    RUN_TEST(test_stream_float32_wav_matches_whole_file);
    RUN_TEST(test_stream_multichannel_wav_matches_whole_file);
    RUN_TEST(test_stream_pcm16_wav_matches_whole_file);
    RUN_TEST(test_stream_bin_matches_whole_file);
    RUN_TEST(test_stream_txt_token_split_across_bites);
    RUN_TEST(test_stream_rejects_unknown_extension);
    RUN_TEST(test_stream_prepare_rejects_unsupported_encoding);
    RUN_TEST(test_stream_read_past_end_starves);
    return UNITY_END();
}
