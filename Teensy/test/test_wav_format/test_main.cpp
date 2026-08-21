// WavFormat: the 44-byte PCM header the SD recorder writes (and patches
// while recording) and the fmt-chunk fields the SD player validates. The
// header a crashed recording leaves behind is only as good as these bytes.

#include <unity.h>

#include <cstring>

#include "WavFormat.h"

// The recorder's canonical stereo 16-bit 44.1kHz header, field by field.
static void test_build_header_canonical_stereo_44k(void) {
    uint8_t h[WavFormat::HEADER_BYTES];
    WavFormat::buildHeader(h, 176400, 44100, 2, 16);

    TEST_ASSERT_EQUAL_MEMORY("RIFF", h, 4);
    TEST_ASSERT_EQUAL_UINT32(36 + 176400, WavFormat::readU32(h + 4));
    TEST_ASSERT_EQUAL_MEMORY("WAVE", h + 8, 4);
    TEST_ASSERT_EQUAL_MEMORY("fmt ", h + 12, 4);
    TEST_ASSERT_EQUAL_UINT32(16, WavFormat::readU32(h + 16));  // fmt size
    TEST_ASSERT_EQUAL_UINT16(1, WavFormat::readU16(h + 20));   // PCM
    TEST_ASSERT_EQUAL_UINT16(2, WavFormat::readU16(h + 22));   // channels
    TEST_ASSERT_EQUAL_UINT32(44100, WavFormat::readU32(h + 24));
    TEST_ASSERT_EQUAL_UINT32(176400, WavFormat::readU32(h + 28)); // byte rate
    TEST_ASSERT_EQUAL_UINT16(4, WavFormat::readU16(h + 32));   // block align
    TEST_ASSERT_EQUAL_UINT16(16, WavFormat::readU16(h + 34));
    TEST_ASSERT_EQUAL_MEMORY("data", h + 36, 4);
    TEST_ASSERT_EQUAL_UINT32(176400, WavFormat::readU32(h + 40));
}

// The patch offsets land exactly on the two running size fields.
static void test_patch_offsets_hit_the_size_fields(void) {
    uint8_t h[WavFormat::HEADER_BYTES];
    WavFormat::buildHeader(h, 0, 44100, 2, 16);

    uint8_t field[4];
    WavFormat::writeU32(field, WavFormat::riffSizeField(88200));
    memcpy(h + WavFormat::RIFF_SIZE_OFFSET, field, 4);
    WavFormat::writeU32(field, WavFormat::dataSizeField(88200));
    memcpy(h + WavFormat::DATA_SIZE_OFFSET, field, 4);

    uint8_t fresh[WavFormat::HEADER_BYTES];
    WavFormat::buildHeader(fresh, 88200, 44100, 2, 16);
    TEST_ASSERT_EQUAL_MEMORY(fresh, h, WavFormat::HEADER_BYTES);
}

// A header round-trips through the player's fmt parse.
static void test_fmt_chunk_round_trips(void) {
    uint8_t h[WavFormat::HEADER_BYTES];
    WavFormat::buildHeader(h, 1000, 44100, 2, 16);

    WavFormat::Fmt fmt;
    // fmt chunk body starts after "fmt " + size, at offset 20
    TEST_ASSERT_TRUE(WavFormat::parseFmtChunk(h + 20, 16, fmt));
    TEST_ASSERT_EQUAL_UINT16(1, fmt.format);
    TEST_ASSERT_EQUAL_UINT16(2, fmt.channels);
    TEST_ASSERT_EQUAL_UINT32(44100, fmt.sampleRate);
    TEST_ASSERT_EQUAL_UINT16(16, fmt.bitsPerSample);
}

static void test_fmt_chunk_rejects_short_body(void) {
    uint8_t body[16] = {0};
    WavFormat::Fmt fmt;
    TEST_ASSERT_FALSE(WavFormat::parseFmtChunk(body, 15, fmt));
}

// Durations round down to whole seconds; a zero rate can't divide by zero.
static void test_data_seconds(void) {
    TEST_ASSERT_EQUAL_UINT32(1, WavFormat::dataSeconds(176400, 44100, 2, 16));
    TEST_ASSERT_EQUAL_UINT32(0, WavFormat::dataSeconds(176399, 44100, 2, 16));
    TEST_ASSERT_EQUAL_UINT32(59, WavFormat::dataSeconds(176400 * 60 - 1, 44100, 2, 16));
    TEST_ASSERT_EQUAL_UINT32(0, WavFormat::dataSeconds(1000, 0, 2, 16));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_build_header_canonical_stereo_44k);
    RUN_TEST(test_patch_offsets_hit_the_size_fields);
    RUN_TEST(test_fmt_chunk_round_trips);
    RUN_TEST(test_fmt_chunk_rejects_short_body);
    RUN_TEST(test_data_seconds);
    return UNITY_END();
}
