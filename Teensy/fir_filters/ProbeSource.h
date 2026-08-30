#ifndef PROBE_SOURCE_H
#define PROBE_SOURCE_H

#include <Arduino.h>
#include <AudioStream.h>
#include "teensy_protocol.h" // PROBE_*/SWEEP_* chirp/schedule contract

// Chirp burst generator shared by the delay alignment probe and the
// measurement sweep (docs/AUTO_FIR_CONTRACTS.md). Plays nChirps identical
// log sweeps at exact sample offsets preRollSamples + k * spacingSamples on
// the audio clock - the fixed spacing is the timing reference the web UI
// measures arrival deviations against, so it must never depend on loop() or
// wall-clock time. Which output each chirp leaves through is handled outside
// (probeLoop solos outputs between bursts); this object only owns the
// waveform and the schedule.
//
// f0Hz/f1Hz/chirpSamples/fadeSamples/preRollSamples/spacingSamples are all
// runtime parameters so the sweep can request its own waveform/timing;
// DelayProbe.cpp's startDelayProbe() passes the PROBE_* constants explicitly
// for the delay probe, so its waveform and timing are unchanged.
class ProbeSource : public AudioStream {
public:
  ProbeSource() : AudioStream(0, nullptr) {}

  // Callers wrap start()/stop() in AudioNoInterrupts()/AudioInterrupts() so
  // the schedule fields become visible to update() atomically.
  void start(uint8_t nChirps, float amplitude, double f0Hz, double f1Hz,
             uint32_t chirpSamples, uint32_t fadeSamples,
             uint32_t preRollSamples, uint32_t spacingSamples);
  void stop();

  bool isRunning() const { return running_; }
  bool isFinished() const { return finished_; }
  uint32_t samplesElapsed() const { return sampleCount_; }

  virtual void update(void) override;

private:
  int16_t chirpSample();

  // Read from loop(), written by update()/start()/stop(); single aligned
  // words, so plain volatile reads are atomic on the M7.
  volatile bool running_ = false;
  volatile bool finished_ = false;
  volatile uint32_t sampleCount_ = 0;

  // update()-context only past this point
  uint8_t nChirps_ = 0;
  uint8_t chirpIdx_ = 0;
  bool inChirp_ = false;
  uint32_t untilChirp_ = 0; // samples of silence left before the next chirp
  uint32_t intoChirp_ = 0;  // samples emitted of the current chirp
  float amp_ = 0.0f;
  // The sweep recurrence runs in double precision so the web UI can
  // reproduce the exact same waveform analytically (JS numbers are doubles).
  double phase_ = 0.0;
  double freq_ = PROBE_F0_HZ;
  double ratio_ = 1.0;

  // Runtime schedule/waveform parameters, set by start(). Defaulted to the
  // delay probe's constants so an un-started object still reports sane
  // values, though nothing reads them before the first start().
  double f0Hz_ = PROBE_F0_HZ;
  double f1Hz_ = PROBE_F1_HZ;
  uint32_t chirpSamples_ = PROBE_CHIRP_SAMPLES;
  uint32_t fadeSamples_ = PROBE_FADE_SAMPLES;
  uint32_t preRollSamples_ = PROBE_PRE_ROLL_SAMPLES;
  uint32_t spacingSamples_ = PROBE_SPACING_SAMPLES;
};

#endif // PROBE_SOURCE_H
