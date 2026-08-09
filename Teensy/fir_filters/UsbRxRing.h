#ifndef USB_RX_RING_H
#define USB_RX_RING_H

#include <stdint.h>

// Hardware-free receive ring + stream state machine for AsyncAudioInputUSB.
// Single producer (the USB interrupt pushes int16 L/R packets, converted to
// float) and single consumer (the audio update drains contiguous runs into
// the resampler). Producer and consumer preempt each other on one core, so
// index reads/writes are safe without masking as long as each side only
// writes its own index (producer: wpos, consumer: rpos).
//
// Stream lifecycle: any write marks the stream active. The consumer detects
// a stopped host (no packet for stopGapMicros - e.g. Spotify paused), drains
// stale samples and re-arms the prefill gate, so playback resumes with a
// full jitter cushion instead of starving immediately.
class UsbRxRing {
public:
    static const uint32_t CAPACITY = 2048; // samples per channel (~46ms at 44.1k), power of two
    static const uint32_t MASK = CAPACITY - 1;

    UsbRxRing(uint32_t prefillSamples, uint32_t stopGapMicros)
        : wpos(0), rpos(0), lastRxMicros(0), active(false), prefilled(false),
          dropCount(0), stopCount(0),
          prefill(prefillSamples), stopGap(stopGapMicros) {}

    // --- producer side (USB interrupt) ---

    // Push one packet of interleaved L/R int16 frames. A packet that doesn't
    // fit is dropped whole (counted); partial packets would corrupt L/R
    // alignment downstream less gracefully than a dropped one.
    bool write(const int16_t* lr, uint32_t frames, uint32_t nowMicros) {
        lastRxMicros = nowMicros;
        active = true;
        const uint32_t free = CAPACITY - (wpos - rpos);
        if (frames > free) {
            dropCount += frames;
            return false;
        }
        uint32_t w = wpos;
        const float scale = 1.0f / 32768.0f;
        for (uint32_t i = 0; i < frames; i++) {
            bufL[w & MASK] = (float)lr[2 * i] * scale;
            bufR[w & MASK] = (float)lr[2 * i + 1] * scale;
            w++;
        }
        wpos = w;
        return true;
    }

    // --- consumer side (audio update) ---

    uint32_t available() const { return wpos - rpos; }

    // Stop detection + prefill gate. Returns true when the consumer should
    // produce output this cycle.
    bool consumerReady(uint32_t nowMicros) {
        if (active && (uint32_t)(nowMicros - lastRxMicros) > stopGap) {
            active = false;
            prefilled = false;
            rpos = wpos; // drain stale audio from the ended stream
            stopCount++;
        }
        if (!active) return false;
        if (!prefilled) {
            if (available() < prefill) return false;
            prefilled = true;
            started = true;
        }
        return true;
    }

    // True exactly once per stream (re)start, on the first ready cycle after
    // the prefill gate opens - the consumer uses it to trim the ring down to
    // its target latency (hosts often front-load tens of ms on stream open).
    bool justStarted() {
        if (!started) return false;
        started = false;
        return true;
    }

    // Contiguous run readable at the current position (stops at the physical
    // end of the ring; call again after consume() for the wrapped remainder).
    uint32_t contiguous() const {
        const uint32_t avail = available();
        const uint32_t untilEnd = CAPACITY - (rpos & MASK);
        return avail < untilEnd ? avail : untilEnd;
    }

    float* leftAt() { return &bufL[rpos & MASK]; }
    float* rightAt() { return &bufR[rpos & MASK]; }

    void consume(uint32_t frames) { rpos += frames; }

    // --- stats ---

    bool streaming() const { return active; }
    uint32_t drops() const { return dropCount; }
    uint32_t stops() const { return stopCount; }

private:
    float bufL[CAPACITY];
    float bufR[CAPACITY];
    volatile uint32_t wpos;
    volatile uint32_t rpos;
    volatile uint32_t lastRxMicros;
    volatile bool active;
    bool prefilled; // consumer-only
    bool started = false; // consumer-only
    volatile uint32_t dropCount;
    uint32_t stopCount; // consumer-only
    const uint32_t prefill;
    const uint32_t stopGap;
};

#endif // USB_RX_RING_H
