#include "RtaFFT4096.h"
#include <arm_const_structs.h>
#include <arm_common_tables.h>

RtaFFT4096::RtaFFT4096()
  : AudioStream(1, inputQueueArray),
    captureFill(0),
    captureReady(false)
{
  capture = new int16_t[FFT_SIZE];
  window = new float[FFT_SIZE];
  work = new float[FFT_SIZE];
  spectrum = new float[FFT_SIZE];
  power = new float[NUM_BINS];

  // Hanning window with the int16 -> float conversion and the FFT
  // normalization folded in. |X| of an amplitude-A sine after a Hanning
  // window is A * N/4 (coherent gain 0.5), so 4/N normalizes a full-scale
  // sine to power 1.0 in its bin.
  const float scale = 4.0f / ((float)FFT_SIZE * 32768.0f);
  for (int i = 0; i < FFT_SIZE; i++) {
    window[i] = (0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (FFT_SIZE - 1))) * scale;
  }
  memset(power, 0, NUM_BINS * sizeof(float));

  // Build the FFT instance by hand instead of calling arm_rfft_fast_init_f32:
  // its size switch links the twiddle tables for every FFT length (see the
  // same trick in FirEngine); referencing the 4096-point tables directly
  // links only what we use.
  rfft.Sint = arm_cfft_sR_f32_len2048;
  rfft.fftLenRFFT = FFT_SIZE;
  rfft.pTwiddleRFFT = (float32_t*)twiddleCoef_rfft_4096;
}

void RtaFFT4096::update(void) {
  audio_block_t* block = receiveReadOnly();
  if (!block) return;
  // A finished capture is waiting for loop-context analyze(): drop the
  // block rather than overwrite it.
  if (!captureReady) {
    memcpy(capture + captureFill, block->data, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
    captureFill += AUDIO_BLOCK_SAMPLES;
    if (captureFill >= FFT_SIZE) {
      captureReady = true;
    }
  }
  release(block);
}

void RtaFFT4096::analyze() {
  if (!captureReady) return;

  for (int i = 0; i < FFT_SIZE; i++) {
    work[i] = capture[i] * window[i];
  }
  // arm_rfft_fast_f32 clobbers its input; both halves of `work` are scratch
  arm_rfft_fast_f32(&rfft, work, spectrum, 0);

  // Packed layout: spectrum[0] = DC, spectrum[1] = Nyquist, then re/im pairs
  power[0] = spectrum[0] * spectrum[0];
  for (int i = 1; i < NUM_BINS; i++) {
    const float re = spectrum[2 * i];
    const float im = spectrum[2 * i + 1];
    power[i] = re * re + im * im;
  }

  captureFill = 0;
  captureReady = false; // reopens the capture buffer to the ISR
}
