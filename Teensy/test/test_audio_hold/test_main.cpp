// AudioHold tests: the three hold sources (boot, nested config syncs, FIR
// load) and the idle fail-safe that releases a hold when the config sync
// never arrives (or its release was lost).

#include <unity.h>

#include "AudioHold.h"

static void test_boot_starts_held(void) {
    AudioHold h;
    TEST_ASSERT_TRUE(h.held());
}

static void test_completed_sync_releases_boot_hold(void) {
    AudioHold h;
    h.beginSync(1000);
    TEST_ASSERT_TRUE(h.held());
    h.endSync();
    TEST_ASSERT_FALSE(h.held());
}

static void test_end_sync_without_begin_releases_boot_hold(void) {
    // A bare "setConfigHold 0" (its 1 lost to a UART glitch) still clears
    // the boot hold: a completed sync is what boot was waiting for.
    AudioHold h;
    h.endSync();
    TEST_ASSERT_FALSE(h.held());
    // And the depth must not have underflowed - the next sync still pairs up
    h.beginSync(0);
    TEST_ASSERT_TRUE(h.held());
    h.endSync();
    TEST_ASSERT_FALSE(h.held());
}

static void test_overlapping_syncs_release_on_the_outer_end(void) {
    // Two overlapping syncs (preset button pressed twice, or a boot event
    // racing an API call): the inner release must not unmute mid-config.
    AudioHold h;
    h.endSync(); // boot done
    h.beginSync(0);
    h.beginSync(0);
    h.endSync();
    TEST_ASSERT_TRUE(h.held());
    h.endSync();
    TEST_ASSERT_FALSE(h.held());
}

static void test_failsafe_releases_after_idle_timeout(void) {
    AudioHold h;
    h.armFailsafe(1000);
    // Exactly at the deadline: still held (strictly-greater-than comparison)
    TEST_ASSERT_FALSE(h.pollFailsafe(0, 1000 + AudioHold::IDLE_TIMEOUT_MS));
    TEST_ASSERT_TRUE(h.held());
    TEST_ASSERT_TRUE(h.pollFailsafe(0, 1001 + AudioHold::IDLE_TIMEOUT_MS));
    TEST_ASSERT_FALSE(h.held());
    // It released once; later polls on an un-held machine stay quiet
    TEST_ASSERT_FALSE(h.pollFailsafe(0, 9999 + AudioHold::IDLE_TIMEOUT_MS));
}

static void test_command_traffic_restarts_the_idle_window(void) {
    AudioHold h;
    h.armFailsafe(0);
    uint32_t t = 0;
    uint32_t dispatched = 0;
    // A sync trickling one command per second stays held indefinitely, even
    // far past the timeout measured from the arm
    for (int i = 0; i < 10; i++) {
        t += 1000;
        dispatched++;
        TEST_ASSERT_FALSE(h.pollFailsafe(dispatched, t));
        TEST_ASSERT_TRUE(h.held());
    }
    // ...and once the traffic stops, the timeout runs from the last command
    TEST_ASSERT_FALSE(h.pollFailsafe(dispatched, t + AudioHold::IDLE_TIMEOUT_MS));
    TEST_ASSERT_TRUE(h.pollFailsafe(dispatched, t + AudioHold::IDLE_TIMEOUT_MS + 1));
    TEST_ASSERT_FALSE(h.held());
}

static void test_failsafe_covers_a_lost_sync_release(void) {
    AudioHold h;
    h.endSync(); // boot done
    h.beginSync(5000); // sync starts; its "setConfigHold 0" never arrives
    // The setConfigHold command itself moved the dispatch count, so the
    // first poll only restarts the idle window...
    TEST_ASSERT_FALSE(h.pollFailsafe(7, 5000));
    // ...and the link then going quiet is what releases the hold
    TEST_ASSERT_TRUE(h.pollFailsafe(7, 5001 + AudioHold::IDLE_TIMEOUT_MS));
    TEST_ASSERT_FALSE(h.held());
}

static void test_fir_load_hold_is_independent_of_the_failsafe(void) {
    AudioHold h;
    h.endSync(); // boot done
    h.setFirLoadHold(true);
    TEST_ASSERT_TRUE(h.held());
    // The fail-safe only covers boot/sync holds: a FIR load's hold is
    // cleared by the load completing in the same loop() pass, never timed
    // out from under it.
    TEST_ASSERT_FALSE(h.pollFailsafe(0, 1000000));
    TEST_ASSERT_TRUE(h.held());
    h.setFirLoadHold(false);
    TEST_ASSERT_FALSE(h.held());
}

static void test_idle_window_survives_millis_wraparound(void) {
    AudioHold h;
    const uint32_t t0 = 0xFFFFFC00u; // ~1s before the 32-bit wrap
    h.armFailsafe(t0);
    TEST_ASSERT_FALSE(h.pollFailsafe(0, t0 + 2000)); // now has wrapped past 0
    TEST_ASSERT_TRUE(h.held());
    TEST_ASSERT_TRUE(h.pollFailsafe(0, t0 + AudioHold::IDLE_TIMEOUT_MS + 1));
    TEST_ASSERT_FALSE(h.held());
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_boot_starts_held);
    RUN_TEST(test_completed_sync_releases_boot_hold);
    RUN_TEST(test_end_sync_without_begin_releases_boot_hold);
    RUN_TEST(test_overlapping_syncs_release_on_the_outer_end);
    RUN_TEST(test_failsafe_releases_after_idle_timeout);
    RUN_TEST(test_command_traffic_restarts_the_idle_window);
    RUN_TEST(test_failsafe_covers_a_lost_sync_release);
    RUN_TEST(test_fir_load_hold_is_independent_of_the_failsafe);
    RUN_TEST(test_idle_window_survives_millis_wraparound);
    return UNITY_END();
}
