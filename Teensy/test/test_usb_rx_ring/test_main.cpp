// UsbRxRing tests: producer/consumer accounting, int16->float conversion,
// full-ring packet drops, the prefill gate, host-stop detection with stale
// drain, and index wraparound (both the ring mask and micros overflow).

#include <unity.h>

#include <cstdint>
#include <vector>

#include "UsbRxRing.h"

namespace {

const uint32_t PREFILL = 512;
const uint32_t STOP_GAP_US = 100000;

// A 44-frame packet (one USB millisecond at 44.1k) with a recognizable ramp
std::vector<int16_t> makePacket(uint32_t frames, int16_t base = 0) {
    std::vector<int16_t> lr(frames * 2);
    for (uint32_t i = 0; i < frames; i++) {
        lr[2 * i] = base + (int16_t)i;        // left: ramp
        lr[2 * i + 1] = -(base + (int16_t)i); // right: mirrored
    }
    return lr;
}

// Feed packets until `frames` frames are written, advancing time by 1ms each
uint32_t feed(UsbRxRing& ring, uint32_t frames, uint32_t nowMicros) {
    while (frames > 0) {
        uint32_t n = frames < 44 ? frames : 44;
        auto p = makePacket(n);
        ring.write(p.data(), n, nowMicros);
        nowMicros += 1000;
        frames -= n;
    }
    return nowMicros;
}

} // namespace

void setUp() {}
void tearDown() {}

// --- write/read accounting and conversion ---

static void test_write_and_available() {
    UsbRxRing ring(PREFILL, STOP_GAP_US);
    TEST_ASSERT_EQUAL_UINT32(0, ring.available());
    auto p = makePacket(44, 100);
    TEST_ASSERT_TRUE(ring.write(p.data(), 44, 1000));
    TEST_ASSERT_EQUAL_UINT32(44, ring.available());
    TEST_ASSERT_EQUAL_UINT32(44, ring.contiguous());
    TEST_ASSERT_TRUE(ring.streaming());
}

static void test_int16_to_float_conversion() {
    UsbRxRing ring(PREFILL, STOP_GAP_US);
    int16_t lr[6] = {32767, -32768, 0, 0, -16384, 16384};
    ring.write(lr, 3, 1000);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 32767.0f / 32768.0f, ring.leftAt()[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -1.0f, ring.rightAt()[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, ring.leftAt()[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.5f, ring.leftAt()[2]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, ring.rightAt()[2]);
}

static void test_consume_advances() {
    UsbRxRing ring(PREFILL, STOP_GAP_US);
    feed(ring, 200, 0);
    ring.consume(150);
    TEST_ASSERT_EQUAL_UINT32(50, ring.available());
}

// --- drops ---

static void test_full_ring_drops_whole_packet() {
    UsbRxRing ring(PREFILL, STOP_GAP_US);
    // Fill to within one packet of capacity
    feed(ring, UsbRxRing::CAPACITY - 20, 0);
    TEST_ASSERT_EQUAL_UINT32(0, ring.drops());
    auto p = makePacket(44);
    TEST_ASSERT_FALSE(ring.write(p.data(), 44, 1000000));
    TEST_ASSERT_EQUAL_UINT32(44, ring.drops());
    // Ring content untouched by the dropped packet
    TEST_ASSERT_EQUAL_UINT32(UsbRxRing::CAPACITY - 20, ring.available());
    // Draining makes room again
    ring.consume(100);
    TEST_ASSERT_TRUE(ring.write(p.data(), 44, 1001000));
}

// --- prefill gate and stop detection ---

static void test_prefill_gate() {
    UsbRxRing ring(PREFILL, STOP_GAP_US);
    TEST_ASSERT_FALSE(ring.consumerReady(0)); // nothing written yet
    TEST_ASSERT_FALSE(ring.justStarted());
    uint32_t now = feed(ring, PREFILL - 44, 0);
    TEST_ASSERT_FALSE(ring.consumerReady(now)); // below prefill
    TEST_ASSERT_FALSE(ring.justStarted());
    now = feed(ring, 44, now);
    TEST_ASSERT_TRUE(ring.consumerReady(now)); // reached prefill
    // justStarted fires exactly once per stream start
    TEST_ASSERT_TRUE(ring.justStarted());
    TEST_ASSERT_FALSE(ring.justStarted());
    // Once open, the gate stays open below the prefill level
    ring.consume(PREFILL - 10);
    TEST_ASSERT_TRUE(ring.consumerReady(now));
    TEST_ASSERT_FALSE(ring.justStarted());
}

static void test_stop_detection_drains_and_rearms() {
    UsbRxRing ring(PREFILL, STOP_GAP_US);
    uint32_t now = feed(ring, PREFILL, 0);
    TEST_ASSERT_TRUE(ring.consumerReady(now));
    ring.consume(100);

    // Host pauses: no packets past the stop gap
    now += STOP_GAP_US + 1000;
    TEST_ASSERT_FALSE(ring.consumerReady(now));
    TEST_ASSERT_FALSE(ring.streaming());
    TEST_ASSERT_EQUAL_UINT32(0, ring.available()); // stale audio drained
    TEST_ASSERT_EQUAL_UINT32(1, ring.stops());

    // Host resumes: gate is armed again, needs a fresh prefill
    now = feed(ring, 44, now);
    TEST_ASSERT_FALSE(ring.consumerReady(now));
    now = feed(ring, PREFILL, now);
    TEST_ASSERT_TRUE(ring.consumerReady(now));
    TEST_ASSERT_TRUE(ring.justStarted()); // re-armed by the restart
    TEST_ASSERT_EQUAL_UINT32(1, ring.stops());
}

static void test_short_gap_is_not_a_stop() {
    UsbRxRing ring(PREFILL, STOP_GAP_US);
    uint32_t now = feed(ring, PREFILL, 0);
    TEST_ASSERT_TRUE(ring.consumerReady(now + STOP_GAP_US - 1000));
    TEST_ASSERT_EQUAL_UINT32(0, ring.stops());
}

// --- wraparound ---

static void test_ring_index_wrap() {
    UsbRxRing ring(PREFILL, STOP_GAP_US);
    // Move the indices near the physical end of the ring
    feed(ring, UsbRxRing::CAPACITY - 30, 0);
    ring.consume(UsbRxRing::CAPACITY - 30);
    TEST_ASSERT_EQUAL_UINT32(0, ring.available());

    // Write 60 frames: 30 before the physical end, 30 after
    auto p = makePacket(60, 500);
    ring.write(p.data(), 60, 1000000);
    TEST_ASSERT_EQUAL_UINT32(60, ring.available());
    TEST_ASSERT_EQUAL_UINT32(30, ring.contiguous()); // capped at the boundary

    // First chunk carries the first 30 samples of the ramp
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 500.0f / 32768.0f, ring.leftAt()[0]);
    ring.consume(30);
    // Wrapped remainder continues the ramp seamlessly
    TEST_ASSERT_EQUAL_UINT32(30, ring.contiguous());
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 530.0f / 32768.0f, ring.leftAt()[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -530.0f / 32768.0f, ring.rightAt()[0]);
}

static void test_micros_wraparound_no_false_stop() {
    UsbRxRing ring(PREFILL, STOP_GAP_US);
    // Last packet just before the 32-bit micros counter wraps
    uint32_t now = 0xFFFFFF00u;
    feed(ring, PREFILL, now - PREFILL / 44 * 1000);
    ring.write(makePacket(44).data(), 44, now);
    // 2ms later, past the wrap: unsigned diff stays small
    TEST_ASSERT_TRUE(ring.consumerReady(0x00000700u));
    TEST_ASSERT_EQUAL_UINT32(0, ring.stops());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_write_and_available);
    RUN_TEST(test_int16_to_float_conversion);
    RUN_TEST(test_consume_advances);
    RUN_TEST(test_full_ring_drops_whole_packet);
    RUN_TEST(test_prefill_gate);
    RUN_TEST(test_stop_detection_drains_and_rearms);
    RUN_TEST(test_short_gap_is_not_a_stop);
    RUN_TEST(test_ring_index_wrap);
    RUN_TEST(test_micros_wraparound_no_false_stop);
    return UNITY_END();
}
