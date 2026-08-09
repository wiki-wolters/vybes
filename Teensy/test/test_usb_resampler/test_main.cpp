// UsbResampler tests. The identity test is the regression net for the
// oversampling-clamp bug: with the shrunken filter table, configure() at
// fs==newFs must reduce the oversampling factor or setFilter() overflows
// the table by ~41KB, corrupting the object (this shipped once - the
// symptom was permanent USB silence). A corrupted resampler cannot pass a
// sample-accurate passthrough check.

#include <unity.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "UsbResampler.h"

namespace {

const int N = 4096;
const int GROUP_DELAY = 20; // = minHalfFilterLength at a 1:1 ratio

// The object is ~71KB - keep it off the test stack. Reconstructed per test
// via placement into fresh state with configure().
UsbResampler resampler(100.0f, 20, 80);

std::vector<float> sine(int n, double freqHz, double fs) {
    std::vector<float> v(n);
    for (int i = 0; i < n; i++) v[i] = 0.5f * (float)sin(2.0 * M_PI * freqHz * i / fs);
    return v;
}

// Push inputs through in odd-sized chunks, harvesting 128-sample output
// blocks - the same call pattern AsyncAudioInputUSB::resampleBlock uses.
void pump(const std::vector<float>& inL, const std::vector<float>& inR,
          std::vector<float>& outL, std::vector<float>& outR) {
    size_t inOff = 0, outN = 0;
    outL.assign(inL.size(), 0.0f);
    outR.assign(inR.size(), 0.0f);
    int stalls = 0;
    while (inOff < inL.size() && outN + 128 <= outL.size()) {
        uint16_t chunk = (uint16_t)std::min<size_t>(173, inL.size() - inOff);
        uint16_t processed = 0, got = 0;
        resampler.resample(const_cast<float*>(inL.data()) + inOff,
                           const_cast<float*>(inR.data()) + inOff, chunk, processed,
                           outL.data() + outN, outR.data() + outN, 128, got);
        inOff += processed;
        outN += got;
        if (processed == 0 && got == 0) {
            if (++stalls > 2) break; // never spin forever on a broken engine
        } else {
            stalls = 0;
        }
    }
    outL.resize(outN);
    outR.resize(outN);
}

} // namespace

void setUp() {}
void tearDown() {}

static void test_configure_1to1_initializes() {
    resampler.configure(44100.0f, 44100.0f);
    TEST_ASSERT_TRUE(resampler.initialized());
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0, resampler.getStep());
}

static void test_identity_passthrough() {
    resampler.configure(44100.0f, 44100.0f);
    // Different tones per channel to catch swaps; 1kHz/2kHz well below Nyquist
    auto inL = sine(N, 1000.0, 44100.0);
    auto inR = sine(N, 2000.0, 44100.0);
    std::vector<float> outL, outR;
    pump(inL, inR, outL, outR);

    // Should produce nearly all of the input (minus lookahead tail)
    TEST_ASSERT_GREATER_THAN_UINT32(N - 512, (uint32_t)outL.size());

    // At step 1.0 the output is the input delayed by the filter's group
    // delay. Skip the initial zero-history transient.
    for (size_t n = 64; n < outL.size(); n++) {
        TEST_ASSERT_FLOAT_WITHIN(2e-3f, inL[n - GROUP_DELAY], outL[n]);
        TEST_ASSERT_FLOAT_WITHIN(2e-3f, inR[n - GROUP_DELAY], outR[n]);
    }
}

static void test_small_diffs_keep_it_alive() {
    resampler.configure(44100.0f, 44100.0f);
    // The clamped servo feeds at most +/-4ms; 100 updates must not trip the
    // 1% kill switch and the step must stay in the adaption band.
    for (int i = 0; i < 100; i++) resampler.addToSampleDiff(0.004);
    TEST_ASSERT_TRUE(resampler.initialized());
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 1.0, resampler.getStep());
}

static void test_kill_switch_and_reconfigure_heal() {
    resampler.configure(44100.0f, 44100.0f);
    // A gross unclamped error (~50ms) requests >1% step: upstream behavior
    // is to permanently deactivate. AsyncAudioInputUSB must heal via
    // configure(), so document both halves here.
    for (int i = 0; i < 50 && resampler.initialized(); i++) {
        resampler.addToSampleDiff(0.05);
    }
    TEST_ASSERT_FALSE(resampler.initialized());
    resampler.configure(44100.0f, 44100.0f);
    TEST_ASSERT_TRUE(resampler.initialized());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_configure_1to1_initializes);
    RUN_TEST(test_identity_passthrough);
    RUN_TEST(test_small_diffs_keep_it_alive);
    RUN_TEST(test_kill_switch_and_reconfigure_heal);
    return UNITY_END();
}
