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
    int64_t dataSizeOverride = -1;   // declared 'data' size, if != actual
};

// Builds RIFF/WAVE with optional junk chunk, fmt, then data.
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

// --- Misc ---

static void test_unsupported_extension_fails_cleanly(void) {
    uint16_t taps = 123;
    MemorySource src(std::string("0.5 0.25"));
    float* coeffs = FIRLoader::loadCoefficients(src, String("coeffs.bin"), taps);
    TEST_ASSERT_NULL(coeffs);
    TEST_ASSERT_EQUAL_UINT16(0, taps);
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
    RUN_TEST(test_valid_txt_loads_exact_coefficients);
    RUN_TEST(test_txt_without_trailing_newline);
    RUN_TEST(test_empty_txt_fails_cleanly);
    RUN_TEST(test_unsupported_extension_fails_cleanly);
    RUN_TEST(test_max_taps_limits_load);
    return UNITY_END();
}
