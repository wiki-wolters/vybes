// KeepaliveStream tests: the enable/keepalive edge detection, the stale-
// keepalive expiry, and the frame-interval pacing shared by the RTA/GRM/VU
// telemetry streamers.

#include <unity.h>

#include "KeepaliveStream.h"

static void test_disabled_by_default(void) {
    KeepaliveStream s(100, 7000);
    TEST_ASSERT_FALSE(s.enabled());
    TEST_ASSERT_FALSE(s.frameDue(123456));
    TEST_ASSERT_FALSE(s.keepaliveExpired(123456));
}

static void test_set_enabled_reports_edges_only(void) {
    // The RTA logs and re-routes its FFT tap on the edge, not on every
    // keepalive - re-binding the tap restarts the FFT accumulation window.
    KeepaliveStream s(100, 7000);
    TEST_ASSERT_TRUE(s.setEnabled(true, 0));
    TEST_ASSERT_FALSE(s.setEnabled(true, 1000)); // keepalive repeat, no edge
    TEST_ASSERT_TRUE(s.setEnabled(false, 2000));
    TEST_ASSERT_FALSE(s.setEnabled(false, 3000));
}

static void test_keepalives_hold_the_stream_open(void) {
    KeepaliveStream s(100, 7000);
    s.setEnabled(true, 0);
    uint32_t t = 0;
    // The ESP refreshes every couple of seconds while a client watches
    for (int i = 0; i < 10; i++) {
        t += 2000;
        TEST_ASSERT_FALSE(s.keepaliveExpired(t));
        s.setEnabled(true, t);
    }
    // Keepalives stop: alive exactly at the deadline, stale just past it
    TEST_ASSERT_FALSE(s.keepaliveExpired(t + 7000));
    TEST_ASSERT_TRUE(s.keepaliveExpired(t + 7001));
}

static void test_frame_pacing(void) {
    KeepaliveStream s(100, 7000);
    s.setEnabled(true, 1000);
    // Nothing sent yet, so the first frame is due immediately
    TEST_ASSERT_TRUE(s.frameDue(1000));
    s.markFrameSent(1000);
    TEST_ASSERT_FALSE(s.frameDue(1099));
    TEST_ASSERT_TRUE(s.frameDue(1100));
    // A skipped frame (busy UART) leaves the next one due until it's sent
    TEST_ASSERT_TRUE(s.frameDue(1150));
    s.markFrameSent(1150);
    TEST_ASSERT_FALSE(s.frameDue(1200));
}

static void test_disable_stops_frames(void) {
    KeepaliveStream s(100, 7000);
    s.setEnabled(true, 0);
    TEST_ASSERT_TRUE(s.frameDue(200));
    s.disable();
    TEST_ASSERT_FALSE(s.enabled());
    TEST_ASSERT_FALSE(s.frameDue(400));
    TEST_ASSERT_FALSE(s.keepaliveExpired(400000));
}

static void test_pacing_survives_millis_wraparound(void) {
    KeepaliveStream s(100, 7000);
    const uint32_t t0 = 0xFFFFFFF0u; // wraps mid-interval
    s.setEnabled(true, t0);
    s.markFrameSent(t0);
    TEST_ASSERT_FALSE(s.frameDue(t0 + 50));
    TEST_ASSERT_TRUE(s.frameDue(t0 + 100));
    TEST_ASSERT_FALSE(s.keepaliveExpired(t0 + 7000));
    TEST_ASSERT_TRUE(s.keepaliveExpired(t0 + 7001));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_disabled_by_default);
    RUN_TEST(test_set_enabled_reports_edges_only);
    RUN_TEST(test_keepalives_hold_the_stream_open);
    RUN_TEST(test_frame_pacing);
    RUN_TEST(test_disable_stops_frames);
    RUN_TEST(test_pacing_survives_millis_wraparound);
    return UNITY_END();
}
