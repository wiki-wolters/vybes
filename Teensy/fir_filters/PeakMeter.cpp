#include "PeakMeter.h"

void PeakMeter::update() {
    for (int ch = 0; ch < 2; ch++) {
        audio_block_t* block = receiveReadOnly(ch);
        if (block == nullptr) continue;

        uint16_t peak = peakAbs[ch];
        int run = 0;
        bool clip = false;
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
            int32_t s = block->data[i];
            uint16_t a = (uint16_t)(s < 0 ? -s : s); // |−32768| fits uint16
            if (a > peak) peak = a;
            if (a >= (uint16_t)CLIP_LEVEL) {
                if (++run >= CLIP_RUN_SAMPLES) clip = true;
            } else {
                run = 0;
            }
        }
        peakAbs[ch] = peak;
        if (clip) clipped[ch] = true;
        release(block);
    }
}

PeakMeter::Reading PeakMeter::read() {
    Reading r;
    __disable_irq();
    for (int ch = 0; ch < 2; ch++) {
        r.peak[ch] = peakAbs[ch] / 32768.0f;
        r.clip[ch] = clipped[ch];
        peakAbs[ch] = 0;
        clipped[ch] = false;
    }
    __enable_irq();
    return r;
}
