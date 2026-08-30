#include "ProbeSource.h"

#include <math.h>

void ProbeSource::start(uint8_t nChirps, float amplitude, double f0Hz, double f1Hz,
                        uint32_t chirpSamples, uint32_t fadeSamples,
                        uint32_t preRollSamples, uint32_t spacingSamples) {
  nChirps_ = nChirps;
  amp_ = amplitude;
  f0Hz_ = f0Hz;
  f1Hz_ = f1Hz;
  chirpSamples_ = chirpSamples;
  fadeSamples_ = fadeSamples;
  preRollSamples_ = preRollSamples;
  spacingSamples_ = spacingSamples;
  chirpIdx_ = 0;
  inChirp_ = false;
  untilChirp_ = preRollSamples_;
  intoChirp_ = 0;
  sampleCount_ = 0;
  ratio_ = exp(log(f1Hz_ / f0Hz_) / (double)chirpSamples_);
  finished_ = false;
  running_ = true;
}

void ProbeSource::stop() {
  running_ = false;
  finished_ = false;
}

// One sample of the current chirp: raised-cosine-faded log sweep. The phase
// recurrence (left Riemann sum of 2*pi*f/fs with f multiplied by a constant
// ratio each sample) is the waveform's definition - the web UI generates its
// matched-filter reference with the same recurrence, and any residual
// mismatch is identical for every chirp so it cancels in the arrival-time
// differences.
int16_t ProbeSource::chirpSample() {
  if (intoChirp_ == 0) {
    phase_ = 0.0;
    freq_ = f0Hz_;
  }
  float w = 1.0f;
  const uint32_t n = intoChirp_;
  if (n < fadeSamples_) {
    w = 0.5f * (1.0f - cosf((float)M_PI * n / fadeSamples_));
  } else if (n > chirpSamples_ - 1 - fadeSamples_) {
    w = 0.5f * (1.0f - cosf((float)M_PI * (chirpSamples_ - 1 - n) / fadeSamples_));
  }
  float s = amp_ * w * sinf((float)phase_);
  phase_ += 2.0 * M_PI * freq_ / (double)PROBE_SAMPLE_RATE;
  if (phase_ >= 2.0 * M_PI) phase_ -= 2.0 * M_PI;
  freq_ *= ratio_;
  intoChirp_++;
  return (int16_t)(s * 32767.0f);
}

void ProbeSource::update(void) {
  if (!running_) return; // no transmit: downstream mixer sees silence

  // Fully silent block: skip the allocation, just advance the clock
  if (!inChirp_ && untilChirp_ >= AUDIO_BLOCK_SAMPLES) {
    untilChirp_ -= AUDIO_BLOCK_SAMPLES;
    sampleCount_ += AUDIO_BLOCK_SAMPLES;
    if (chirpIdx_ >= nChirps_ && untilChirp_ == 0) {
      running_ = false;
      finished_ = true;
    }
    return;
  }

  audio_block_t* block = allocate();
  if (!block) { // pool exhausted; keep the clock honest and carry on
    sampleCount_ += AUDIO_BLOCK_SAMPLES;
    return;
  }

  for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
    int16_t s = 0;
    if (inChirp_) {
      s = chirpSample();
      if (intoChirp_ >= chirpSamples_) {
        inChirp_ = false;
        chirpIdx_++;
        untilChirp_ = (chirpIdx_ < nChirps_)
                          ? spacingSamples_ - chirpSamples_
                          : PROBE_TAIL_SAMPLES;
      }
    } else if (untilChirp_ > 0) {
      untilChirp_--;
      if (untilChirp_ == 0 && chirpIdx_ >= nChirps_) {
        running_ = false;
        finished_ = true;
      }
    } else if (chirpIdx_ < nChirps_) {
      inChirp_ = true;
      intoChirp_ = 0;
      s = chirpSample();
    }
    block->data[i] = s;
  }
  sampleCount_ += AUDIO_BLOCK_SAMPLES;
  transmit(block);
  release(block);
}
