#ifndef ASYNC_AUDIO_INPUT_USB_H
#define ASYNC_AUDIO_INPUT_USB_H

#include <Arduino.h>
#include <AudioStream.h>
#include <Quantizer.h>
#include "UsbResampler.h"
#include "UsbRxRing.h"

// USB audio input that is immune to host clock drift and packet-delivery
// jitter. The core fork's usb_audio_rx_hook hands every raw isochronous
// packet to this class before the stock AudioInputUSB buffering; packets go
// into a ~46ms ring (UsbRxRing) and update() resamples to the local audio
// clock with the same resampler/quantizer machinery AsyncAudioInputSPDIF3
// uses (UsbResampler is a slimmed vendored copy), servoing the resample step
// on ring fill level. The USB feedback endpoint keeps reporting nominal
// 44.1kHz - all rate matching happens here, so it also works with hosts
// that ignore the feedback endpoint.
//
// The UsbResampler (~71KB) and the ring live on the heap (RAM2), keeping
// RAM1 untouched and leaving the full 12288-tap FIR pool viable. Host
// volume/mute (the USB feature unit) is ignored, matching how the sketch
// uses AudioInputUSB - input gain lives in the mixers.
//
// Only one instance may exist (the hook has a single consumer slot); with no
// instance the hook declines packets and the stock AudioInputUSB path works
// unchanged.
class AsyncAudioInputUSB : public AudioStream {
public:
    AsyncAudioInputUSB(bool dither = true, bool noiseshaping = true,
                       float attenuation = 100.0f,
                       int32_t minHalfFilterLength = 20,
                       int32_t maxHalfFilterLength = 80);
    ~AsyncAudioInputUSB();
    virtual void update(void);

    bool streaming() const { return ring && ring->streaming(); }
    float bufferedMs() const;
    double stepPpm() const;   // resample step offset from 1.0 in ppm (= measured host clock offset)
    uint32_t drops() const { return ring ? ring->drops() : 0; }   // frames dropped, ring full
    uint32_t stops() const { return ring ? ring->stops() : 0; }   // stream stop/start transitions
    uint32_t starves() const { return starveCount; }              // blocks padded with silence mid-stream

    // Called by the core fork's usb_audio_rx_hook (USB interrupt context).
    // Returns nonzero when the packet was consumed.
    static int rxHook(const int16_t* lr, unsigned int frames);

private:
    void servo();
    int resampleBlock(int16_t* dstL, int16_t* dstR);

    static AsyncAudioInputUSB* instance;

    UsbRxRing* ring;
    UsbResampler* resampler;
    Quantizer* quantizer[2];
    float diffFiltered;    // low-passed latency error fed to the step PID
    double targetLatencyS;
    double maxLatencyS;
    uint32_t starveCount;
};

#endif // ASYNC_AUDIO_INPUT_USB_H
