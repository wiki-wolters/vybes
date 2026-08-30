// FirUpload state-machine tests: the full firPutBegin/firPut/firPutEnd/
// firPutAbort/firDelete grammar (docs/AUTO_FIR_CONTRACTS.md, Slice A),
// exercised against an in-memory fake FirUploadStorage so no real SD card
// is involved - mirrors the CoeffSource split FIRLoader already uses.

#include <unity.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "FirUpload.h"
#include "teensy_protocol.h" // crc32*/base64* helpers, FIR_PUT_*/FIR_UPLOAD_* constants

// --- fake storage ---

class FakeFirUploadStorage : public FirUploadStorage {
public:
    bool readyFlag = true;
    bool busyFlag = false;
    bool openTempResult = true;
    bool writeTempResult = true;
    bool renameResult = true;

    std::string tempContent;
    bool tempOpen = false;
    std::map<std::string, std::string> files;

    bool ready() override { return readyFlag; }
    bool busy() override { return busyFlag; }

    bool openTemp() override {
        tempContent.clear();
        tempOpen = openTempResult;
        return openTempResult;
    }
    bool writeTemp(const uint8_t* data, size_t len) override {
        if (!tempOpen || !writeTempResult) return false;
        tempContent.append((const char*)data, len);
        return true;
    }
    void closeTemp() override { tempOpen = false; }
    void removeTemp() override { tempContent.clear(); }

    bool exists(const char* name) override { return files.count(name) > 0; }
    bool removeFile(const char* name) override { return files.erase(name) > 0; }
    bool renameTempTo(const char* name) override {
        if (!renameResult) return false;
        files[name] = tempContent;
        tempContent.clear();
        return true;
    }
    long countTaps(const char* name, uint32_t sizeBytes) override {
        (void)name;
        (void)sizeBytes;
        return 0; // every test file here is .bin, which never calls this
    }
};

// OutputStream capturing everything a handler writes, split into lines for
// easy assertions (mirrors test_command_router's CaptureStream).
class CaptureStream : public OutputStream {
public:
    size_t write(uint8_t c) override { data += (char)c; return 1; }
    size_t write(const char* d, size_t len) override { data.append(d, len); return len; }
    size_t write(const uint8_t* buffer, size_t size) override {
        data.append((const char*)buffer, size);
        return size;
    }
    std::string data;

    // The last complete line written (without its trailing newline), or ""
    // if nothing was written. Handlers here only ever write one line.
    std::string lastLine() const {
        std::string s = data;
        while (!s.empty() && s.back() == '\n') s.pop_back();
        return s;
    }
};

static FakeFirUploadStorage storage;

void setUp(void) {
    storage = FakeFirUploadStorage();
    firUploadSetStorage(&storage);
    firUploadResetForTest();
}

void tearDown(void) {}

// --- helpers ---

static String* makeArgs(const std::vector<std::string>& values) {
    String* args = new String[values.size()];
    for (size_t i = 0; i < values.size(); i++) args[i] = String(values[i].c_str());
    return args;
}

static void freeArgs(String* args) { delete[] args; }

static std::string crcHexOf(const std::string& data) {
    char hex[9];
    crc32ToHex(crc32Of((const uint8_t*)data.data(), data.size()), hex);
    return std::string(hex);
}

static std::string b64Of(const uint8_t* data, size_t len) {
    char buf[FIR_PUT_B64_MAX + 1];
    size_t n = base64Encode(data, len, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    return std::string(buf, n);
}

// Runs firPutBegin for a named payload (its size and crc32 computed from
// the payload itself) and returns the capture stream for assertions.
static CaptureStream doBegin(const std::string& name, const std::string& payload) {
    CaptureStream out;
    std::vector<std::string> vals = {
        name, std::to_string(payload.size()), crcHexOf(payload)
    };
    String* args = makeArgs(vals);
    handleFirPutBegin(String("firPutBegin"), args, 3, out);
    freeArgs(args);
    return out;
}

// Sends the whole payload as FIR_PUT_CHUNK_BYTES-sized firPut lines,
// starting at seq 0. Returns the capture stream from the LAST line sent
// (callers needing every ACK should drive the lines themselves).
static CaptureStream sendAllLines(const std::string& payload, int* lineCountOut = nullptr) {
    CaptureStream out;
    size_t offset = 0;
    int seq = 0;
    while (offset < payload.size()) {
        size_t n = std::min((size_t)FIR_PUT_CHUNK_BYTES, payload.size() - offset);
        std::string b64 = b64Of((const uint8_t*)payload.data() + offset, n);
        out = CaptureStream();
        std::vector<std::string> vals = {std::to_string(seq), b64};
        String* args = makeArgs(vals);
        handleFirPut(String("firPut"), args, 2, out);
        freeArgs(args);
        offset += n;
        seq++;
    }
    if (lineCountOut) *lineCountOut = seq;
    return out;
}

static CaptureStream doEnd() {
    CaptureStream out;
    handleFirPutEnd(String("firPutEnd"), nullptr, 0, out);
    return out;
}

static std::string makePayload(size_t len, uint8_t seed = 0x11) {
    std::string s;
    s.resize(len);
    for (size_t i = 0; i < len; i++) s[i] = (char)(seed + i);
    return s;
}

// --- happy path ---

static void test_happy_path_uploads_and_verifies(void) {
    const std::string payload = makePayload(8); // 8 bytes = 2 float32 taps
    CaptureStream begin = doBegin("a1-test.bin", payload);
    TEST_ASSERT_EQUAL_STRING("FIRPUT BEGIN a1-test.bin", begin.lastLine().c_str());

    int lines = 0;
    CaptureStream lastPut = sendAllLines(payload, &lines);
    TEST_ASSERT_EQUAL_INT(1, lines); // 8 bytes fits in one line
    TEST_ASSERT_EQUAL_STRING("FIRPUT ACK 0", lastPut.lastLine().c_str()); // final line always ACKs

    CaptureStream end = doEnd();
    TEST_ASSERT_EQUAL_STRING("FIRPUT OK a1-test.bin 8 2", end.lastLine().c_str());

    TEST_ASSERT_TRUE(storage.exists("a1-test.bin"));
    TEST_ASSERT_EQUAL_STRING(payload.c_str(), storage.files["a1-test.bin"].c_str());
    TEST_ASSERT_TRUE(storage.tempContent.empty());
}

static void test_ack_cadence_every_stride_and_final_line(void) {
    // 20 lines of 45 bytes each (900 bytes, a whole number of float32 taps)
    // - only line 16 (seq 15) and the final line (seq 19) should ACK.
    const std::string payload = makePayload(20 * FIR_PUT_CHUNK_BYTES);
    doBegin("a2-cadence.bin", payload);

    size_t offset = 0;
    for (int seq = 0; seq < 20; seq++) {
        std::string b64 = b64Of((const uint8_t*)payload.data() + offset, FIR_PUT_CHUNK_BYTES);
        offset += FIR_PUT_CHUNK_BYTES;
        CaptureStream out;
        std::vector<std::string> vals = {std::to_string(seq), b64};
        String* args = makeArgs(vals);
        handleFirPut(String("firPut"), args, 2, out);
        freeArgs(args);

        char msg[64];
        snprintf(msg, sizeof(msg), "seq %d", seq);
        if (seq == 15 || seq == 19) {
            char expected[32];
            snprintf(expected, sizeof(expected), "FIRPUT ACK %d", seq);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, out.lastLine().c_str(), msg);
        } else {
            TEST_ASSERT_EQUAL_STRING_MESSAGE("", out.data.c_str(), msg);
        }
    }
    CaptureStream end = doEnd();
    TEST_ASSERT_EQUAL_STRING("FIRPUT OK a2-cadence.bin 900 225", end.lastLine().c_str());
}

// --- error paths ---

static void test_seq_gap_aborts_and_resets(void) {
    const std::string payload = makePayload(8);
    doBegin("a3-gap.bin", payload);

    CaptureStream out;
    std::vector<std::string> vals = {"1", b64Of((const uint8_t*)payload.data(), 8)}; // should be seq 0
    String* args = makeArgs(vals);
    handleFirPut(String("firPut"), args, 2, out);
    freeArgs(args);
    TEST_ASSERT_EQUAL_STRING("FIRPUT ERR badSeq", out.lastLine().c_str());
    TEST_ASSERT_TRUE(storage.tempContent.empty()); // torn down, not left dangling

    // Session reset: a fresh begin works cleanly afterward.
    CaptureStream begin2 = doBegin("a3-gap.bin", payload);
    TEST_ASSERT_EQUAL_STRING("FIRPUT BEGIN a3-gap.bin", begin2.lastLine().c_str());
}

static void test_seq_repeat_aborts(void) {
    // Claimed size (48, a valid .bin multiple of 4) is bigger than the one
    // 45-byte line actually sent, so that line is neither final nor a
    // stride boundary - no ACK expected before the repeat.
    const std::string payload = makePayload(48);
    doBegin("a4-repeat.bin", payload);
    std::string b64_0 = b64Of((const uint8_t*)payload.data(), FIR_PUT_CHUNK_BYTES);

    CaptureStream first;
    std::vector<std::string> v0 = {"0", b64_0};
    String* a0 = makeArgs(v0);
    handleFirPut(String("firPut"), a0, 2, first);
    freeArgs(a0);
    TEST_ASSERT_TRUE(first.data.empty()); // not a stride/final line - no ACK yet

    // Repeat seq 0 instead of advancing to 1.
    CaptureStream repeat;
    std::vector<std::string> vr = {"0", b64_0};
    String* ar = makeArgs(vr);
    handleFirPut(String("firPut"), ar, 2, repeat);
    freeArgs(ar);
    TEST_ASSERT_EQUAL_STRING("FIRPUT ERR badSeq", repeat.lastLine().c_str());
}

static void test_crc_mismatch_leaves_no_target_file(void) {
    const std::string payload = makePayload(8);
    CaptureStream out;
    // Deliberately wrong (but well-formed) crc32.
    std::vector<std::string> vals = {"a5-crc.bin", "8", "deadbeef"};
    String* args = makeArgs(vals);
    handleFirPutBegin(String("firPutBegin"), args, 3, out);
    freeArgs(args);
    TEST_ASSERT_EQUAL_STRING("FIRPUT BEGIN a5-crc.bin", out.lastLine().c_str());

    sendAllLines(payload);
    CaptureStream end = doEnd();
    TEST_ASSERT_EQUAL_STRING("FIRPUT ERR crc", end.lastLine().c_str());
    TEST_ASSERT_FALSE(storage.exists("a5-crc.bin"));
    TEST_ASSERT_TRUE(storage.tempContent.empty());
}

static void test_oversize_line_rejected_as_badb64(void) {
    doBegin("a6-oversize.bin", makePayload(48)); // claimed size, a valid .bin multiple of 4

    std::string tooLong(FIR_PUT_B64_MAX + 1, 'A'); // one char past the wire limit
    CaptureStream out;
    std::vector<std::string> vals = {"0", tooLong};
    String* args = makeArgs(vals);
    handleFirPut(String("firPut"), args, 2, out);
    freeArgs(args);
    TEST_ASSERT_EQUAL_STRING("FIRPUT ERR badB64", out.lastLine().c_str());
}

static void test_malformed_base64_rejected(void) {
    doBegin("a7-badb64.bin", makePayload(48)); // claimed size, a valid .bin multiple of 4

    CaptureStream out;
    std::vector<std::string> vals = {"0", "not-valid-base64!!"};
    String* args = makeArgs(vals);
    handleFirPut(String("firPut"), args, 2, out);
    freeArgs(args);
    TEST_ASSERT_EQUAL_STRING("FIRPUT ERR badB64", out.lastLine().c_str());
}

static void test_more_bytes_than_claimed_size_is_badsize(void) {
    // Claim 4 bytes but send a full 45-byte line - the decoded chunk alone
    // already overruns the declared size.
    doBegin("a8-oversize-total.bin", makePayload(4));

    CaptureStream out;
    std::string b64 = b64Of((const uint8_t*)makePayload(FIR_PUT_CHUNK_BYTES).data(), FIR_PUT_CHUNK_BYTES);
    std::vector<std::string> vals = {"0", b64};
    String* args = makeArgs(vals);
    handleFirPut(String("firPut"), args, 2, out);
    freeArgs(args);
    TEST_ASSERT_EQUAL_STRING("FIRPUT ERR badSize", out.lastLine().c_str());
}

static void test_abort_stops_and_resets(void) {
    // Claimed size (180, a valid .bin multiple of 4) is bigger than the one
    // line actually sent, so the upload is genuinely partial when aborted.
    doBegin("a9-abort.bin", makePayload(FIR_PUT_CHUNK_BYTES * 4));
    sendAllLines(makePayload(FIR_PUT_CHUNK_BYTES)); // partial data written

    CaptureStream out;
    handleFirPutAbort(String("firPutAbort"), nullptr, 0, out);
    TEST_ASSERT_EQUAL_STRING("FIRPUT STOP", out.lastLine().c_str());
    TEST_ASSERT_TRUE(storage.tempContent.empty());
    TEST_ASSERT_FALSE(storage.exists("a9-abort.bin"));

    // Idempotent even with nothing running.
    CaptureStream again;
    handleFirPutAbort(String("firPutAbort"), nullptr, 0, again);
    TEST_ASSERT_EQUAL_STRING("FIRPUT STOP", again.lastLine().c_str());

    // A fresh begin works cleanly afterward.
    CaptureStream begin2 = doBegin("a9-abort.bin", makePayload(4));
    TEST_ASSERT_EQUAL_STRING("FIRPUT BEGIN a9-abort.bin", begin2.lastLine().c_str());
}

static void test_busy_while_recording_or_playing(void) {
    storage.busyFlag = true;
    CaptureStream out = doBegin("b1-busy.bin", makePayload(4));
    TEST_ASSERT_EQUAL_STRING("FIRPUT ERR busy", out.lastLine().c_str());
}

static void test_begin_while_writing_is_busy(void) {
    doBegin("b2-first.bin", makePayload(48)); // claimed size, a valid .bin multiple of 4
    CaptureStream out = doBegin("b2-second.bin", makePayload(4));
    TEST_ASSERT_EQUAL_STRING("FIRPUT ERR busy", out.lastLine().c_str());
}

static void test_sd_not_ready(void) {
    storage.readyFlag = false;
    CaptureStream out = doBegin("b3-nosd.bin", makePayload(4));
    TEST_ASSERT_EQUAL_STRING("FIRPUT ERR sd", out.lastLine().c_str());
}

static void test_open_temp_failure_is_sd(void) {
    storage.openTempResult = false;
    CaptureStream out = doBegin("b4-noopen.bin", makePayload(4));
    TEST_ASSERT_EQUAL_STRING("FIRPUT ERR sd", out.lastLine().c_str());
}

static void test_write_failure_is_nospace(void) {
    doBegin("b5-full.bin", makePayload(48)); // claimed size, a valid .bin multiple of 4
    storage.writeTempResult = false;

    CaptureStream out;
    std::string b64 = b64Of((const uint8_t*)makePayload(FIR_PUT_CHUNK_BYTES).data(), FIR_PUT_CHUNK_BYTES);
    std::vector<std::string> vals = {"0", b64};
    String* args = makeArgs(vals);
    handleFirPut(String("firPut"), args, 2, out);
    freeArgs(args);
    TEST_ASSERT_EQUAL_STRING("FIRPUT ERR noSpace", out.lastLine().c_str());
}

static void test_bad_name_rejected(void) {
    // A single token (survives the router's whitespace tokenization intact)
    // that still isn't a valid SD-root filename: a path separator.
    CaptureStream out;
    std::vector<std::string> vals = {"sub/dir.bin", "4", crcHexOf(makePayload(4))};
    String* args = makeArgs(vals);
    handleFirPutBegin(String("firPutBegin"), args, 3, out);
    freeArgs(args);
    TEST_ASSERT_EQUAL_STRING("FIRPUT ERR badName", out.lastLine().c_str());
}

static void test_bad_size_bounds_and_bin_alignment(void) {
    // Below the minimum.
    CaptureStream tooSmall;
    std::vector<std::string> v1 = {"c1-small.bin", "3", "00000000"};
    String* a1 = makeArgs(v1);
    handleFirPutBegin(String("firPutBegin"), a1, 3, tooSmall);
    freeArgs(a1);
    TEST_ASSERT_EQUAL_STRING("FIRPUT ERR badSize", tooSmall.lastLine().c_str());

    // Above the maximum.
    CaptureStream tooBig;
    std::vector<std::string> v2 = {"c2-big.bin", "51204", "00000000"};
    String* a2 = makeArgs(v2);
    handleFirPutBegin(String("firPutBegin"), a2, 3, tooBig);
    freeArgs(a2);
    TEST_ASSERT_EQUAL_STRING("FIRPUT ERR badSize", tooBig.lastLine().c_str());

    // A .bin size must be a multiple of 4.
    CaptureStream misaligned;
    std::vector<std::string> v3 = {"c3-odd.bin", "5", "00000000"};
    String* a3 = makeArgs(v3);
    handleFirPutBegin(String("firPutBegin"), a3, 3, misaligned);
    freeArgs(a3);
    TEST_ASSERT_EQUAL_STRING("FIRPUT ERR badSize", misaligned.lastLine().c_str());
}

// --- out-of-state ---

static void test_firput_without_begin_is_state_error(void) {
    CaptureStream out;
    std::vector<std::string> vals = {"0", b64Of((const uint8_t*)"abc", 3)};
    String* args = makeArgs(vals);
    handleFirPut(String("firPut"), args, 2, out);
    freeArgs(args);
    TEST_ASSERT_EQUAL_STRING("FIRPUT ERR state", out.lastLine().c_str());
}

static void test_firputend_without_begin_is_state_error(void) {
    CaptureStream out = doEnd();
    TEST_ASSERT_EQUAL_STRING("FIRPUT ERR state", out.lastLine().c_str());
}

// --- firDelete ---

static void test_delete_ok_and_notfound_and_badname(void) {
    const std::string payload = makePayload(8);
    doBegin("d1-delete.bin", payload);
    sendAllLines(payload);
    doEnd();
    TEST_ASSERT_TRUE(storage.exists("d1-delete.bin"));

    CaptureStream out;
    std::vector<std::string> vals = {"d1-delete.bin"};
    String* args = makeArgs(vals);
    handleFirDelete(String("firDelete"), args, 1, out);
    freeArgs(args);
    TEST_ASSERT_EQUAL_STRING("FIRDEL OK d1-delete.bin", out.lastLine().c_str());
    TEST_ASSERT_FALSE(storage.exists("d1-delete.bin"));

    CaptureStream again;
    String* args2 = makeArgs(vals);
    handleFirDelete(String("firDelete"), args2, 1, again);
    freeArgs(args2);
    TEST_ASSERT_EQUAL_STRING("FIRDEL ERR notfound", again.lastLine().c_str());

    CaptureStream badName;
    std::vector<std::string> badVals = {"sub/dir.bin"};
    String* args3 = makeArgs(badVals);
    handleFirDelete(String("firDelete"), args3, 1, badName);
    freeArgs(args3);
    TEST_ASSERT_EQUAL_STRING("FIRDEL ERR badName", badName.lastLine().c_str());
}

static void test_delete_busy_while_uploading(void) {
    doBegin("d2-busy.bin", makePayload(48)); // claimed size, a valid .bin multiple of 4

    CaptureStream out;
    std::vector<std::string> vals = {"d2-busy.bin"};
    String* args = makeArgs(vals);
    handleFirDelete(String("firDelete"), args, 1, out);
    freeArgs(args);
    TEST_ASSERT_EQUAL_STRING("FIRDEL ERR busy", out.lastLine().c_str());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_happy_path_uploads_and_verifies);
    RUN_TEST(test_ack_cadence_every_stride_and_final_line);
    RUN_TEST(test_seq_gap_aborts_and_resets);
    RUN_TEST(test_seq_repeat_aborts);
    RUN_TEST(test_crc_mismatch_leaves_no_target_file);
    RUN_TEST(test_oversize_line_rejected_as_badb64);
    RUN_TEST(test_malformed_base64_rejected);
    RUN_TEST(test_more_bytes_than_claimed_size_is_badsize);
    RUN_TEST(test_abort_stops_and_resets);
    RUN_TEST(test_busy_while_recording_or_playing);
    RUN_TEST(test_begin_while_writing_is_busy);
    RUN_TEST(test_sd_not_ready);
    RUN_TEST(test_open_temp_failure_is_sd);
    RUN_TEST(test_write_failure_is_nospace);
    RUN_TEST(test_bad_name_rejected);
    RUN_TEST(test_bad_size_bounds_and_bin_alignment);
    RUN_TEST(test_firput_without_begin_is_state_error);
    RUN_TEST(test_firputend_without_begin_is_state_error);
    RUN_TEST(test_delete_ok_and_notfound_and_badname);
    RUN_TEST(test_delete_busy_while_uploading);
    return UNITY_END();
}
