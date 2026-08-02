#ifndef RTA_FFT_4096_H
#define RTA_FFT_4096_H

#include <Audio.h>
#include <arm_math.h>

// 4096-point spectrum tap for the RTA. Replaces AudioAnalyzeFFT1024, whose
// 43Hz bins are coarser than the 1/12-octave RTA bands everywhere below
// ~500Hz - exactly where room modes live. 4096 points at 44.1kHz gives
// 10.77Hz bins.
//
// The audio ISR only copies samples (update() appends each 128-sample block
// to a capture buffer); the FFT itself runs in loop context via analyze().
// While a full capture waits to be analyzed, incoming blocks are dropped -
// at a ~10Hz frame rate the next window starts almost immediately after.
class RtaFFT4096 : public AudioStream {
public:
  static const int FFT_SIZE = 4096;
  static const int NUM_BINS = FFT_SIZE / 2; // 2048

  RtaFFT4096();

  virtual void update(void) override;

  // True once a full 4096-sample capture is waiting for analyze()
  bool available() const { return captureReady; }

  // Window + FFT + per-bin power. Call from loop context when available();
  // frees the capture buffer for the next window.
  void analyze();

  // Power (normalized magnitude squared) of one bin after analyze(). A
  // full-scale sine reads ~1.0 in its bin, matching AudioAnalyzeFFT1024's
  // read() normalization so the dB scale on the wire stays comparable.
  float readPower(int bin) const {
    return (bin >= 0 && bin < NUM_BINS) ? power[bin] : 0.0f;
  }

  static float binWidthHz() { return AUDIO_SAMPLE_RATE_EXACT / (float)FFT_SIZE; }

private:
  audio_block_t* inputQueueArray[1];
  int16_t* capture;          // samples being collected (written by the ISR)
  float* window;             // Hanning, with the int16 and FFT scaling folded in
  float* work;               // windowed input scratch (clobbered by the FFT)
  float* spectrum;           // packed complex FFT output
  float* power;              // per-bin normalized power
  volatile int captureFill;
  volatile bool captureReady;
  arm_rfft_fast_instance_f32 rfft;
};

#endif // RTA_FFT_4096_H
