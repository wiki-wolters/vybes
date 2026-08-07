#include "ProbeSource.h"

#include <math.h>

void ProbeSource::start(uint8_t nChirps, float amplitude) {
  nChirps_ = nChirps;
  amp_ = amplitude;
  chirpIdx_ = 0;
  inChirp_ = false;
  untilChirp_ = PROBE_PRE_ROLL_SAMPLES;
  intoChirp_ = 0;
  sampleCount_ = 0;
  ratio_ = exp(log(PROBE_F1_HZ / PROBE_F0_HZ) / (double)PROBE_CHIRP_SAMPLES);
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
    freq_ = PROBE_F0_HZ;
  }
  float w = 1.0f;
  const uint32_t n = intoChirp_;
  if (n < PROBE_FADE_SAMPLES) {
    w = 0.5f * (1.0f - cosf((float)M_PI * n / PROBE_FADE_SAMPLES));
  } else if (n > PROBE_CHIRP_SAMPLES - 1 - PROBE_FADE_SAMPLES) {
    w = 0.5f * (1.0f - cosf((float)M_PI * (PROBE_CHIRP_SAMPLES - 1 - n) / PROBE_FADE_SAMPLES));
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
      if (intoChirp_ >= PROBE_CHIRP_SAMPLES) {
        inChirp_ = false;
        chirpIdx_++;
        untilChirp_ = (chirpIdx_ < nChirps_)
                          ? PROBE_SPACING_SAMPLES - PROBE_CHIRP_SAMPLES
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
