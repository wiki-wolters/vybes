#ifndef PEAK_METER_H
#define PEAK_METER_H

// Stereo peak meter with clip detection, tapped off the L/R input mixers
// (the summing point that actually saturates when sources stack up).
//
// update() tracks, per channel, the largest absolute sample and whether the
// block contained a flat-topped run of full-scale samples - AudioMixer4
// saturates by clamping, so a clipped waveform sits at +/-32767 for several
// consecutive samples, which a merely-hot signal touching full scale for a
// sample or two does not. That distinction is what lets the UI show
// "close to clipping" (peak near 0dBFS) separately from "clipping" (plateau
// seen).
//
// Peaks accumulate max-wise between read() calls, so no transient is missed
// however slowly the meter is polled. Holds no blocks and allocates nothing.

#include <Arduino.h>
#include <Audio.h>

class PeakMeter : public AudioStream {
public:
    struct Reading {
        float peak[2] = {0.0f, 0.0f}; // linear 0..1 per channel
        bool clip[2] = {false, false};
    };

    PeakMeter() : AudioStream(2, inputQueueArray) {}

    // Peak and clip state accumulated since the previous read (resets both).
    Reading read();

    virtual void update() override;

private:
    // Samples at or above this count as full scale (saturation clamps to
    // exactly 32767 / -32768, but allow one LSB of slack).
    static const int16_t CLIP_LEVEL = 32766;
    // A run of this many consecutive full-scale samples is a clipped
    // waveform's plateau, not a legitimate peak brushing 0dBFS.
    static const int CLIP_RUN_SAMPLES = 4;

    audio_block_t* inputQueueArray[2];
    volatile uint16_t peakAbs[2] = {0, 0};
    volatile bool clipped[2] = {false, false};
};

#endif // PEAK_METER_H
