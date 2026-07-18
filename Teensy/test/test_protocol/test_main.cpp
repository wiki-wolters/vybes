// Serial protocol round-trip: every command the ESP defines (CMD_* in
// ESP/esp-web-server/teensy_protocol.h) is formatted with the ESP's real
// message builder (teensyBuildMessage) and fed byte-for-byte through the
// Teensy's real line handling (SerialCommandRouter::loop), asserting it
// dispatches to a handler registered from the Teensy's command table
// (TeensyCommands.h) with the expected argument count. Also covers message
// length limits, newline framing, \r tolerance and truncation behavior.

#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "SerialCommandRouter.h"
#include "TeensyCommands.h"
#include "teensy_protocol.h" // the ESP side (via -I../ESP/esp-web-server)

// The Teensy's registered command names, straight from the shared table
static const char* const kTeensyCommands[] = {
#define VYBES_COMMAND_NAME(name, handler) #name,
    TEENSY_COMMAND_LIST(VYBES_COMMAND_NAME)
#undef VYBES_COMMAND_NAME
};
static const int kTeensyCommandCount = sizeof(kTeensyCommands) / sizeof(kTeensyCommands[0]);

// --- capture of what the router dispatched ---
static std::string lastCommand;
static std::vector<std::string> lastArgs;
static int dispatchCount = 0;

static void resetCapture() {
    lastCommand.clear();
    lastArgs.clear();
    dispatchCount = 0;
}

static void captureHandler(const String& command, String* args, int argCount, OutputStream&) {
    dispatchCount++;
    lastCommand = command.c_str();
    lastArgs.clear();
    for (int i = 0; i < argCount; i++) lastArgs.push_back(args[i].c_str());
}

// A router with every Teensy command registered, fed by a fake serial port
struct TestRig {
    HardwareSerial port;
    SerialCommandRouter router;
    TestRig() : router(port) {
        for (int i = 0; i < kTeensyCommandCount; i++) {
            router.on(kTeensyCommands[i], captureHandler);
        }
    }
};

// Build a message the way the ESP does and push it through the Teensy router
static size_t roundTrip(TestRig& rig, const char* cmd,
                        const char* p1 = nullptr, const char* p2 = nullptr,
                        const char* p3 = nullptr, const char* p4 = nullptr,
                        const char* p5 = nullptr, bool* truncated = nullptr) {
    char msg[TEENSY_MSG_MAX];
    size_t len = teensyBuildMessage(msg, sizeof(msg), cmd, p1, p2, p3, p4, p5, truncated);
    rig.port.feedInput(msg, len);
    rig.router.loop();
    return len;
}

static void assertDispatched(const char* cmd, int expectedArgs) {
    char msg[96];
    snprintf(msg, sizeof(msg), "command '%s'", cmd);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, dispatchCount, msg);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(cmd, lastCommand.c_str(), msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(expectedArgs, (int)lastArgs.size(), msg);
}

// One entry per CMD_* the ESP defines, with representative parameters
struct ProtocolCase {
    const char* cmd;
    const char* p1, *p2, *p3, *p4, *p5;
    int expectedArgs;
};

static const ProtocolCase kProtocolCases[] = {
    {CMD_SET_SPEAKER_GAINS, "1.00", "0.90", "0.80", nullptr, nullptr, 3},
    {CMD_SET_INPUT_GAINS, "1.00", "0.50", "0.75", "1.00", "0.25", 5},
    {CMD_SET_VOLUME, "0.75", nullptr, nullptr, nullptr, nullptr, 1},
    {CMD_SET_CROSSOVER_FREQ, "80", nullptr, nullptr, nullptr, nullptr, 1},
    {CMD_SET_CROSSOVER_ENABLED, "1", nullptr, nullptr, nullptr, nullptr, 1},
    {CMD_SET_EQ_ENABLED, "1", nullptr, nullptr, nullptr, nullptr, 1},
    {CMD_SET_EQ_FILTER, "3", "1000", "1.5", "-4.5", nullptr, 4},
    {CMD_RESET_EQ_FILTERS, "0", nullptr, nullptr, nullptr, nullptr, 1},
    {CMD_SET_FIR, "left", "DeskL.wav", nullptr, nullptr, nullptr, 2},
    {CMD_SET_FIR_ENABLED, "1", nullptr, nullptr, nullptr, nullptr, 1},
    {CMD_LOAD_FIR_FILES, nullptr, nullptr, nullptr, nullptr, nullptr, 0},
    {CMD_GET_FILES, nullptr, nullptr, nullptr, nullptr, nullptr, 0},
    {CMD_SET_DELAYS, "100", "200", "300", nullptr, nullptr, 3},
    {CMD_SET_DELAY_ENABLED, "1", nullptr, nullptr, nullptr, nullptr, 1},
    {CMD_SET_TONE, "1000.00", "50.00", nullptr, nullptr, nullptr, 2},
    {CMD_STOP_TONE, nullptr, nullptr, nullptr, nullptr, nullptr, 0},
    {CMD_SET_NOISE, "25.00", nullptr, nullptr, nullptr, nullptr, 1},
    {CMD_SET_RTA, "1", nullptr, nullptr, nullptr, nullptr, 1},
    {CMD_SET_MUTE, "1", nullptr, nullptr, nullptr, nullptr, 1},
    {CMD_SET_MUTE_PERCENT, "50.00", nullptr, nullptr, nullptr, nullptr, 1},
    {CMD_PING, nullptr, nullptr, nullptr, nullptr, nullptr, 0},
};
static const int kProtocolCaseCount = sizeof(kProtocolCases) / sizeof(kProtocolCases[0]);

// Every ESP command round-trips into the matching Teensy handler with the
// expected number of arguments.
static void test_every_esp_command_round_trips(void) {
    for (int i = 0; i < kProtocolCaseCount; i++) {
        const ProtocolCase& c = kProtocolCases[i];
        TestRig rig;
        resetCapture();
        bool truncated = true;
        roundTrip(rig, c.cmd, c.p1, c.p2, c.p3, c.p4, c.p5, &truncated);
        TEST_ASSERT_FALSE_MESSAGE(truncated, c.cmd);
        assertDispatched(c.cmd, c.expectedArgs);
    }
}

// The two command lists cover each other exactly: every ESP CMD_* appears in
// the Teensy table (implied by the round-trip test), and every Teensy table
// entry is exercised by an ESP CMD_* above - so neither side can grow a
// command the other doesn't know about without this failing.
static void test_command_tables_cover_each_other(void) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(kTeensyCommandCount, kProtocolCaseCount,
                                  "ESP CMD_* count vs Teensy command table");
    for (int t = 0; t < kTeensyCommandCount; t++) {
        bool found = false;
        for (int c = 0; c < kProtocolCaseCount; c++) {
            if (strcmp(kTeensyCommands[t], kProtocolCases[c].cmd) == 0) {
                found = true;
                break;
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(found, kTeensyCommands[t]);
    }
}

// The ESP's message limit must fit the Teensy's line buffer with headroom
static void test_message_size_headroom(void) {
    TEST_ASSERT_TRUE(TEENSY_MSG_MAX <= SerialCommandRouter::LINE_BUFFER_SIZE);
    // ...and even a maximal (truncated) ESP message plus \r fits
    TEST_ASSERT_TRUE(TEENSY_MSG_MAX + 1 < SerialCommandRouter::LINE_BUFFER_SIZE);
}

// A maximum-length untruncated message: "setFir right <63-char filename>"
// is the longest realistic command and must survive intact.
static void test_max_length_setfir_round_trips(void) {
    std::string filename(63, 'f');
    filename.replace(59, 4, ".wav");
    TestRig rig;
    resetCapture();
    bool truncated = true;
    size_t len = roundTrip(rig, CMD_SET_FIR, "right", filename.c_str(),
                           nullptr, nullptr, nullptr, &truncated);
    TEST_ASSERT_FALSE(truncated);
    TEST_ASSERT_TRUE(len < TEENSY_MSG_MAX); // fits the ESP buffer
    assertDispatched(CMD_SET_FIR, 2);
    TEST_ASSERT_EQUAL_STRING(filename.c_str(), lastArgs[1].c_str());
}

// An over-long message is truncated by the ESP but stays newline-terminated,
// so the Teensy still frames and dispatches it (with a shortened argument)
// instead of desynchronizing the stream.
static void test_overlong_message_truncates_but_keeps_framing(void) {
    std::string filename(100, 'g');
    char msg[TEENSY_MSG_MAX];
    bool truncated = false;
    size_t len = teensyBuildMessage(msg, sizeof(msg), CMD_SET_FIR, "right",
                                    filename.c_str(), nullptr, nullptr, nullptr, &truncated);
    TEST_ASSERT_TRUE(truncated);
    TEST_ASSERT_EQUAL_UINT32(TEENSY_MSG_MAX - 1, (uint32_t)len);
    TEST_ASSERT_EQUAL_CHAR('\n', msg[len - 1]);
    TEST_ASSERT_EQUAL_CHAR('\0', msg[len]);

    TestRig rig;
    resetCapture();
    rig.port.feedInput(msg, len);
    rig.router.loop();
    assertDispatched(CMD_SET_FIR, 2);
    // The argument was truncated, not padded or corrupted
    TEST_ASSERT_TRUE(lastArgs[1].size() < filename.size());
    TEST_ASSERT_EQUAL_STRING(std::string(lastArgs[1].size(), 'g').c_str(), lastArgs[1].c_str());
}

// Newline framing: two messages in one burst dispatch separately, a partial
// message without its newline waits for the rest.
static void test_newline_framing_across_bursts(void) {
    TestRig rig;
    resetCapture();
    char m1[TEENSY_MSG_MAX], m2[TEENSY_MSG_MAX];
    size_t l1 = teensyBuildMessage(m1, sizeof(m1), CMD_SET_MUTE, "1",
                                   nullptr, nullptr, nullptr, nullptr, nullptr);
    size_t l2 = teensyBuildMessage(m2, sizeof(m2), CMD_SET_VOLUME, "0.50",
                                   nullptr, nullptr, nullptr, nullptr, nullptr);

    // Burst 1: all of message 1 plus the first half of message 2
    std::string burst1 = std::string(m1, l1) + std::string(m2, 5);
    rig.port.feedInput(burst1.c_str(), burst1.size());
    rig.router.loop();
    TEST_ASSERT_EQUAL_INT(1, dispatchCount);
    TEST_ASSERT_EQUAL_STRING(CMD_SET_MUTE, lastCommand.c_str());

    // Burst 2: the rest of message 2
    rig.port.feedInput(m2 + 5, l2 - 5);
    rig.router.loop();
    TEST_ASSERT_EQUAL_INT(2, dispatchCount);
    TEST_ASSERT_EQUAL_STRING(CMD_SET_VOLUME, lastCommand.c_str());
    TEST_ASSERT_EQUAL_STRING("0.50", lastArgs[0].c_str());
}

// \r tolerance: a peer sending CRLF line endings must still dispatch cleanly
static void test_carriage_return_tolerance(void) {
    TestRig rig;
    resetCapture();
    std::string msg = std::string(CMD_SET_DELAYS) + " 100 200 300\r\n";
    rig.port.feedInput(msg.c_str(), msg.size());
    rig.router.loop();
    assertDispatched(CMD_SET_DELAYS, 3);
}

// A line longer than the Teensy's 256-byte buffer is dropped without
// desynchronizing subsequent commands.
static void test_teensy_drops_overlong_line_and_recovers(void) {
    TestRig rig;
    resetCapture();
    std::string junk(SerialCommandRouter::LINE_BUFFER_SIZE + 40, 'j');
    junk += "\n";
    rig.port.feedInput(junk.c_str(), junk.size());
    rig.router.loop();
    TEST_ASSERT_EQUAL_INT(0, dispatchCount);

    roundTrip(rig, CMD_PING);
    assertDispatched(CMD_PING, 0);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_every_esp_command_round_trips);
    RUN_TEST(test_command_tables_cover_each_other);
    RUN_TEST(test_message_size_headroom);
    RUN_TEST(test_max_length_setfir_round_trips);
    RUN_TEST(test_overlong_message_truncates_but_keeps_framing);
    RUN_TEST(test_newline_framing_across_bursts);
    RUN_TEST(test_carriage_return_tolerance);
    RUN_TEST(test_teensy_drops_overlong_line_and_recovers);
    return UNITY_END();
}
