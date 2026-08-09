#include "AsyncAudioInputUSB.h"

// Host nominal rate: fixed by the USB descriptor (44.1kHz). The host's real
// clock deviates by tens of ppm; the step PID absorbs that (and far more -
// UsbResampler allows 1% adaption).
static const double USB_NOMINAL_HZ = 44100.0;

// Steady-state ring fill the servo holds (= added input latency) and the
// hard ceiling beyond which we resync by dropping excess (host burst far
// bigger than the cushion, or fill runaway before the servo converged).
static const double TARGET_LATENCY_S = 0.008;
static const double MAX_LATENCY_S = 0.030;

// Prefill before output resumes after a stream (re)start: the target fill
// plus one output block and the resampler's lookahead, so the first blocks
// don't immediately starve.
static const uint32_t PREFILL_SAMPLES = 512;

// No packet for this long = host stopped the stream (pause, track gap).
static const uint32_t STOP_GAP_US = 100000;

// One-pole smoothing of the latency error before the step PID (~5Hz at the
// 344.5Hz update rate), standing in for AsyncAudioInputSPDIF3's biquad: the
// raw fill sawtooths by a packet (~1ms) depending on update/packet phase.
static const float DIFF_LPF_ALPHA = 0.09f;

AsyncAudioInputUSB* AsyncAudioInputUSB::instance = nullptr;

extern "C" int usb_audio_rx_hook(const int16_t* lr, unsigned int frames)
{
    return AsyncAudioInputUSB::rxHook(lr, frames);
}

int AsyncAudioInputUSB::rxHook(const int16_t* lr, unsigned int frames)
{
    AsyncAudioInputUSB* self = instance;
    if (self == nullptr || self->ring == nullptr) return 0; // stock path takes it
    self->ring->write(lr, frames, micros());
    return 1; // consumed (a full-ring drop is counted, not retried)
}

AsyncAudioInputUSB::AsyncAudioInputUSB(bool dither, bool noiseshaping,
                                       float attenuation,
                                       int32_t minHalfFilterLength,
                                       int32_t maxHalfFilterLength)
    : AudioStream(0, NULL),
      diffFiltered(0.0f),
      targetLatencyS(TARGET_LATENCY_S),
      maxLatencyS(MAX_LATENCY_S),
      starveCount(0)
{
    ring = new UsbRxRing(PREFILL_SAMPLES, STOP_GAP_US);
    resampler = new UsbResampler(attenuation, minHalfFilterLength, maxHalfFilterLength);
    const float factor = powf(2, 15) - 1.f; // to 16 bit audio
    quantizer[0] = new Quantizer(AUDIO_SAMPLE_RATE_EXACT);
    quantizer[0]->configure(noiseshaping, dither, factor);
    quantizer[1] = new Quantizer(AUDIO_SAMPLE_RATE_EXACT);
    quantizer[1]->configure(noiseshaping, dither, factor);
    resampler->configure(USB_NOMINAL_HZ, AUDIO_SAMPLE_RATE_EXACT);
    instance = this;
}

AsyncAudioInputUSB::~AsyncAudioInputUSB()
{
    instance = nullptr;
    delete quantizer[0];
    delete quantizer[1];
    delete resampler;
    delete ring;
}

float AsyncAudioInputUSB::bufferedMs() const
{
    return ring ? (float)(ring->available() * (1000.0 / USB_NOMINAL_HZ)) : 0.0f;
}

double AsyncAudioInputUSB::stepPpm() const
{
    return resampler ? (resampler->getStep() - 1.0) * 1e6 : 0.0;
}

// Latency servo, the counterpart of AsyncAudioInputSPDIF3's
// monitorResampleBuffer(): low-pass the fill error and feed it to the
// resampler's step PID; on gross overshoot, resync hard by dropping down to
// the target.
void AsyncAudioInputUSB::servo()
{
    const double bufferedS = ring->available() / USB_NOMINAL_HZ;
    const double diff = bufferedS - targetLatencyS;
    diffFiltered += DIFF_LPF_ALPHA * ((float)diff - diffFiltered);
    resampler->addToSampleDiff(diffFiltered);

    if (bufferedS > maxLatencyS) {
        const uint32_t target = (uint32_t)(targetLatencyS * USB_NOMINAL_HZ);
        const uint32_t avail = ring->available();
        if (avail > target) ring->consume(avail - target);
        diffFiltered = 0.0f;
        resampler->fixStep();
    }
}

// Fill one output block through the resampler, in up to two passes when the
// readable run wraps around the physical end of the ring (the resampler
// carries its fractional position and filter history across calls).
int AsyncAudioInputUSB::resampleBlock(int16_t* dstL, int16_t* dstR)
{
    if (!resampler->initialized()) return 0;
    float outL[AUDIO_BLOCK_SAMPLES];
    float outR[AUDIO_BLOCK_SAMPLES];
    int filled = 0;
    for (int pass = 0; pass < 2 && filled < AUDIO_BLOCK_SAMPLES; pass++) {
        const uint32_t run = ring->contiguous();
        if (run == 0) break;
        uint16_t processed = 0;
        uint16_t got = 0;
        resampler->resample(ring->leftAt(), ring->rightAt(), (uint16_t)run, processed,
                            outL + filled, outR + filled,
                            (uint16_t)(AUDIO_BLOCK_SAMPLES - filled), got);
        ring->consume(processed);
        filled += got;
        if (processed < run) break; // stopped on lookahead, not on the wrap
    }
    quantizer[0]->quantize(outL, dstL, filled);
    quantizer[1]->quantize(outR, dstR, filled);
    return filled;
}

void AsyncAudioInputUSB::update(void)
{
    if (ring == nullptr || resampler == nullptr) return;
    if (!ring->consumerReady(micros())) {
        // idle or prefilling: transmit nothing (silence downstream, like
        // AudioInputUSB with no data) and keep the servo state neutral
        diffFiltered = 0.0f;
        return;
    }
    servo();

    audio_block_t* left = allocate();
    if (left == nullptr) return;
    audio_block_t* right = allocate();
    if (right == nullptr) {
        release(left);
        return;
    }
    const int filled = resampleBlock(left->data, right->data);
    if (filled < AUDIO_BLOCK_SAMPLES) {
        memset(left->data + filled, 0, (AUDIO_BLOCK_SAMPLES - filled) * sizeof(int16_t));
        memset(right->data + filled, 0, (AUDIO_BLOCK_SAMPLES - filled) * sizeof(int16_t));
        starveCount++;
    }
    transmit(left, 0);
    release(left);
    transmit(right, 1);
    release(right);
}
