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

// Hard bound on the latency error fed to the step PID. UsbResampler's
// addToSampleDiff PERMANENTLY deactivates the resampler when the requested
// step deviates >1% from configured (maxAdaption); with kp=0.6 a raw error
// above ~16.6ms would reach that. Clamped at 4ms the worst-case correction
// (kp + kd on the clamped slope + bounded integral) stays under ~0.7%, so
// the kill switch is unreachable in normal operation - and update() heals
// by reconfiguring if it ever fires anyway.
static const float DIFF_CLAMP_S = 0.004f;

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
      starveCount(0),
      recoveryCount(0),
      healNeeded(false),
      stepAtKillPpm(0.0f),
      updatesSinceFix(0)
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

// Called from loop(): performs the expensive resampler heal outside the
// audio interrupt. While healNeeded is set, update() skips the resampler
// (initialized() is false), so configure() can rebuild the tables with
// interrupts enabled; only the final resync runs atomically.
void AsyncAudioInputUSB::healPending()
{
    if (!healNeeded || ring == nullptr || resampler == nullptr) return;
    Serial.print("USB in: resampler heal (step was ");
    Serial.print(stepAtKillPpm, 1);
    Serial.print(" ppm, buffered ");
    Serial.print(bufferedMs(), 1);
    Serial.println(" ms)");
    resampler->configure(USB_NOMINAL_HZ, AUDIO_SAMPLE_RATE_EXACT);
    __disable_irq();
    resyncToTarget();
    recoveryCount++;
    healNeeded = false;
    __enable_irq();
}

float AsyncAudioInputUSB::bufferedMs() const
{
    return ring ? (float)(ring->available() * (1000.0 / USB_NOMINAL_HZ)) : 0.0f;
}

double AsyncAudioInputUSB::stepPpm() const
{
    return resampler ? (resampler->getStep() - 1.0) * 1e6 : 0.0;
}

// Drop ring content down to the target latency and neutralize the servo
// state (fixStep bakes the current adaption in as the new baseline and
// clears the PID integrator).
void AsyncAudioInputUSB::resyncToTarget()
{
    const uint32_t target = (uint32_t)(targetLatencyS * USB_NOMINAL_HZ);
    const uint32_t avail = ring->available();
    if (avail > target) {
        // Skipping samples is an instantaneous waveform discontinuity - one
        // audible click. Counted so it can never hide behind clean
        // drop/starve counters.
        resyncCount++;
        lastResyncFillMs = (float)(avail * (1000.0 / USB_NOMINAL_HZ));
        ring->consume(avail - target);
    }
    diffFiltered = 0.0f;
    resampler->fixStep();
}

// Latency servo, the counterpart of AsyncAudioInputSPDIF3's
// monitorResampleBuffer(): on gross overshoot resync hard first (so the PID
// never sees the excursion), otherwise low-pass and clamp the fill error
// and feed it to the resampler's step PID.
void AsyncAudioInputUSB::servo()
{
    const double bufferedS = ring->available() / USB_NOMINAL_HZ;
    if (bufferedS > maxLatencyS) {
        resyncToTarget();
        return;
    }
    const double diff = bufferedS - targetLatencyS;
    diffFiltered += DIFF_LPF_ALPHA * ((float)diff - diffFiltered);
    float fed = diffFiltered;
    if (fed > DIFF_CLAMP_S) fed = DIFF_CLAMP_S;
    if (fed < -DIFF_CLAMP_S) fed = -DIFF_CLAMP_S;
    resampler->addToSampleDiff(fed);
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

// Glitch reporter. Runs in loop(), never in the audio/USB interrupts where
// Serial output would deadlock: polls the counters at ~25ms and prints the
// instant one moves, placing a single audible click in time to 25ms.
//
// Deliberately event-driven rather than periodic. Teensy's USB CDC write
// blocks for up to TX_TIMEOUT (~120ms) when a host holds the port open but
// stops draining it, and a stall that long in loop() coarsens
// updateAudioVolume()'s gain smoothing - i.e. a debug print that causes the
// very artefact it is here to find. These lines only fire when something has
// already gone wrong; steady-state health belongs in the 20s summary.
void AsyncAudioInputUSB::diagLoop()
{
    static uint32_t lastPoll = 0;
    static uint32_t pDrop = 0, pStarve = 0, pStop = 0, pResync = 0,
                    pAlloc = 0, pRecov = 0;
    const uint32_t now = millis();
    if ((uint32_t)(now - lastPoll) < 25) return;
    lastPoll = now;

    const uint32_t cDrop = drops(), cStarve = starves(), cStop = stops(),
                   cResync = resyncCount, cAlloc = allocFailCount,
                   cRecov = recoveryCount;
    if (cDrop != pDrop || cStarve != pStarve || cStop != pStop ||
        cResync != pResync || cAlloc != pAlloc || cRecov != pRecov) {
        Serial.printf("UEVT %lu drop+%lu starve+%lu(filled %u) stop+%lu "
                      "resync+%lu(fill %ld) alloc+%lu heal+%lu fill %ld step %ld\n",
                      (unsigned long)now,
                      (unsigned long)(cDrop - pDrop),
                      (unsigned long)(cStarve - pStarve), (unsigned)lastStarveFilled,
                      (unsigned long)(cStop - pStop),
                      (unsigned long)(cResync - pResync), (long)(lastResyncFillMs * 10),
                      (unsigned long)(cAlloc - pAlloc),
                      (unsigned long)(cRecov - pRecov),
                      (long)(bufferedMs() * 10), (long)stepPpm());
        pDrop = cDrop; pStarve = cStarve; pStop = cStop;
        pResync = cResync; pAlloc = cAlloc; pRecov = cRecov;
    }

    // The verdict line for a stop: how long the host was actually silent.
    // ~100ms+ means the host really stalled; ~1ms means the stop detector
    // fired while packets were still arriving normally.
    static uint32_t pResumeSeq = 0;
    if (ring->resumeSeqNo() != pResumeSeq) {
        pResumeSeq = ring->resumeSeqNo();
        Serial.printf("URESUME %lu host silence %lu us, packets %lu\n",
                      (unsigned long)now, (unsigned long)ring->resumeGap(),
                      (unsigned long)ring->packets());
    }

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
    if (!resampler->initialized()) {
        // The step PID's kill switch fired (addToSampleDiff deactivates on
        // >1% step requests). Healing means re-running configure(), a
        // tens-of-ms Kaiser recomputation that would stall EVERY audio
        // stream if done here - flag it for healPending() in loop() and
        // output silence until it completes.
        if (!healNeeded) {
            healNeeded = true;
            stepAtKillPpm = (float)((resampler->getStep() - 1.0) * 1e6);
        }
        // Keep draining the ring to target meanwhile: loop() can be blocked
        // for seconds (FIR loads from SD at boot), and without consumption
        // the ring overflows and every packet drops until the heal runs.
        const uint32_t target = (uint32_t)(targetLatencyS * USB_NOMINAL_HZ);
        const uint32_t avail = ring->available();
        if (avail > target) ring->consume(avail - target);
        return;
    }
    if (ring->justStarted()) {
        // Stream (re)start: hosts front-load tens of ms on stream open;
        // trim straight to the target so the servo starts from zero error.
        resyncToTarget();
    }
    servo();

    // Anti-windup, the counterpart of AsyncAudioInputSPDIF3's settled
    // fixStep(): with the error settled, periodically bake the adapted step
    // in as the new baseline and clear the PID integrator, so slow integral
    // windup can never creep toward the 1% kill switch.
    if (++updatesSinceFix >= 4096) { // ~12s
        updatesSinceFix = 0;
        if (fabsf(diffFiltered) < 0.0005f) { resampler->fixStep(); fixStepCount++; }
    }

    audio_block_t* left = allocate();
    if (left == nullptr) { allocFailCount++; return; }
    audio_block_t* right = allocate();
    if (right == nullptr) {
        allocFailCount++;
        release(left);
        return;
    }
    const int filled = resampleBlock(left->data, right->data);
    if (filled < AUDIO_BLOCK_SAMPLES) {
        memset(left->data + filled, 0, (AUDIO_BLOCK_SAMPLES - filled) * sizeof(int16_t));
        memset(right->data + filled, 0, (AUDIO_BLOCK_SAMPLES - filled) * sizeof(int16_t));
        starveCount++;
        lastStarveFilled = (uint16_t)filled;
    }
    transmit(left, 0);
    release(left);
    transmit(right, 1);
    release(right);
}
