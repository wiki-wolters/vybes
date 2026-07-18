// SerialCommandRouter tests: parseArgs tokenization and line handling.

#include <unity.h>

#include <string>
#include <vector>

#include "SerialCommandRouter.h"

// OutputStream capturing everything a handler writes
class CaptureStream : public OutputStream {
public:
    size_t write(uint8_t c) override { data += (char)c; return 1; }
    size_t write(const char* d, size_t len) override { data.append(d, len); return len; }
    size_t write(const uint8_t* buffer, size_t size) override {
        data.append((const char*)buffer, size);
        return size;
    }
    std::string data;
};

// Last dispatch captured by the test handler
static std::string lastCommand;
static std::vector<std::string> lastArgs;
static bool lastArgsWasNull = false;
static int dispatchCount = 0;

static void resetCapture() {
    lastCommand.clear();
    lastArgs.clear();
    lastArgsWasNull = false;
    dispatchCount = 0;
}

static void captureHandler(const String& command, String* args, int argCount, OutputStream&) {
    dispatchCount++;
    lastCommand = command.c_str();
    lastArgs.clear();
    lastArgsWasNull = (args == nullptr);
    for (int i = 0; i < argCount; i++) lastArgs.push_back(args[i].c_str());
}

// --- parseArgs ---

static HardwareSerial testPort;

static void checkArgs(const char* input, const std::vector<std::string>& expected) {
    SerialCommandRouter router(testPort);
    int count = -1;
    String* args = router.parseArgs(String(input), count);
    char msg[128];
    snprintf(msg, sizeof(msg), "parseArgs(\"%s\") count", input);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)expected.size(), count, msg);
    if (expected.empty()) {
        TEST_ASSERT_NULL_MESSAGE(args, msg);
        return;
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(args, msg);
    for (size_t i = 0; i < expected.size(); i++) {
        TEST_ASSERT_EQUAL_STRING_MESSAGE(expected[i].c_str(), args[i].c_str(), msg);
    }
    delete[] args;
}

static void test_parse_args_single(void) {
    checkArgs("one", {"one"});
}

static void test_parse_args_multiple(void) {
    checkArgs("a b c", {"a", "b", "c"});
}

static void test_parse_args_repeated_internal_spaces(void) {
    checkArgs("a   b  c", {"a", "b", "c"});
}

static void test_parse_args_leading_and_trailing_spaces(void) {
    checkArgs("  a b  ", {"a", "b"});
    checkArgs(" x", {"x"});
    checkArgs("x ", {"x"});
}

static void test_parse_args_all_spaces_yields_null(void) {
    checkArgs("    ", {});
}

static void test_parse_args_empty_string_yields_null(void) {
    checkArgs("", {});
}

static void test_parse_args_mixed_lengths(void) {
    checkArgs("left  DeskL.wav", {"left", "DeskL.wav"});
    checkArgs("3 1000.5  -4.25   0.7", {"3", "1000.5", "-4.25", "0.7"});
}

// --- processCommand dispatch ---

static void test_process_command_dispatches_with_args(void) {
    resetCapture();
    SerialCommandRouter router(testPort);
    router.on("setEq", captureHandler);
    CaptureStream out;
    router.processCommand(String("setEq 3 1000 1.5 -4.5"), out);
    TEST_ASSERT_EQUAL_INT(1, dispatchCount);
    TEST_ASSERT_EQUAL_STRING("setEq", lastCommand.c_str());
    TEST_ASSERT_EQUAL_INT(4, (int)lastArgs.size());
    TEST_ASSERT_EQUAL_STRING("1000", lastArgs[1].c_str());
}

static void test_process_command_no_args_passes_null(void) {
    resetCapture();
    SerialCommandRouter router(testPort);
    router.on("ping", captureHandler);
    CaptureStream out;
    router.processCommand(String("ping"), out);
    TEST_ASSERT_EQUAL_INT(1, dispatchCount);
    TEST_ASSERT_TRUE(lastArgsWasNull);
    TEST_ASSERT_EQUAL_INT(0, (int)lastArgs.size());
}

static void test_process_command_repeated_spaces_between_args(void) {
    resetCapture();
    SerialCommandRouter router(testPort);
    router.on("setDelays", captureHandler);
    CaptureStream out;
    router.processCommand(String("setDelays   100  200      300"), out);
    TEST_ASSERT_EQUAL_INT(1, dispatchCount);
    TEST_ASSERT_EQUAL_INT(3, (int)lastArgs.size());
    TEST_ASSERT_EQUAL_STRING("100", lastArgs[0].c_str());
    TEST_ASSERT_EQUAL_STRING("300", lastArgs[2].c_str());
}

static void test_process_command_is_case_insensitive(void) {
    resetCapture();
    SerialCommandRouter router(testPort);
    router.on("setMute", captureHandler);
    CaptureStream out;
    router.processCommand(String("SETMUTE 1"), out);
    TEST_ASSERT_EQUAL_INT(1, dispatchCount);
    TEST_ASSERT_EQUAL_INT(1, (int)lastArgs.size());
}

static void test_unknown_command_not_dispatched(void) {
    resetCapture();
    SerialCommandRouter router(testPort);
    router.on("ping", captureHandler);
    CaptureStream out;
    router.processCommand(String("nonsense 1 2"), out);
    TEST_ASSERT_EQUAL_INT(0, dispatchCount);
}

// --- loop() line framing ---

static void test_loop_dispatches_newline_terminated_lines(void) {
    resetCapture();
    HardwareSerial port;
    SerialCommandRouter router(port);
    router.on("setVolume", captureHandler);
    router.on("ping", captureHandler);
    port.feedInput("setVolume 0.75\nping\n");
    router.loop();
    TEST_ASSERT_EQUAL_INT(2, dispatchCount);
    TEST_ASSERT_EQUAL_STRING("ping", lastCommand.c_str());
}

static void test_loop_ignores_carriage_returns(void) {
    resetCapture();
    HardwareSerial port;
    SerialCommandRouter router(port);
    router.on("setMute", captureHandler);
    port.feedInput("setMute 1\r\n");
    router.loop();
    TEST_ASSERT_EQUAL_INT(1, dispatchCount);
    TEST_ASSERT_EQUAL_INT(1, (int)lastArgs.size());
    TEST_ASSERT_EQUAL_STRING("1", lastArgs[0].c_str());
}

static void test_loop_drops_overlong_line_then_recovers(void) {
    resetCapture();
    HardwareSerial port;
    SerialCommandRouter router(port);
    router.on("ping", captureHandler);
    std::string longLine(SerialCommandRouter::LINE_BUFFER_SIZE + 50, 'x');
    longLine += "\nping\n";
    port.feedInput(longLine.c_str());
    router.loop();
    TEST_ASSERT_EQUAL_INT(1, dispatchCount); // only the ping
    TEST_ASSERT_EQUAL_STRING("ping", lastCommand.c_str());
}

static void test_loop_skips_blank_lines(void) {
    resetCapture();
    HardwareSerial port;
    SerialCommandRouter router(port);
    router.on("ping", captureHandler);
    port.feedInput("\n\r\n   \nping\n");
    router.loop();
    TEST_ASSERT_EQUAL_INT(1, dispatchCount);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_args_single);
    RUN_TEST(test_parse_args_multiple);
    RUN_TEST(test_parse_args_repeated_internal_spaces);
    RUN_TEST(test_parse_args_leading_and_trailing_spaces);
    RUN_TEST(test_parse_args_all_spaces_yields_null);
    RUN_TEST(test_parse_args_empty_string_yields_null);
    RUN_TEST(test_parse_args_mixed_lengths);
    RUN_TEST(test_process_command_dispatches_with_args);
    RUN_TEST(test_process_command_no_args_passes_null);
    RUN_TEST(test_process_command_repeated_spaces_between_args);
    RUN_TEST(test_process_command_is_case_insensitive);
    RUN_TEST(test_unknown_command_not_dispatched);
    RUN_TEST(test_loop_dispatches_newline_terminated_lines);
    RUN_TEST(test_loop_ignores_carriage_returns);
    RUN_TEST(test_loop_skips_blank_lines);
    RUN_TEST(test_loop_drops_overlong_line_then_recovers);
    return UNITY_END();
}
