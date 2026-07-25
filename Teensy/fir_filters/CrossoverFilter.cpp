#include "CrossoverFilter.h"

CrossoverFilter::CrossoverFilter()
  : AudioStream(1, inputQueueArray), sampleRate(44100.0f) {
  hp.count = 0;
  lp.count = 0;
  for (int i = 0; i < 2; i++) {
    hpState[i] = {0.0f, 0.0f};
    lpState[i] = {0.0f, 0.0f};
  }
}

void CrossoverFilter::begin(float rate) {
  sampleRate = rate;
}

void CrossoverFilter::applyBranch(XoverBranch& target, XoverSectionState* states,
                                  float freq, CrossoverType type) {
  XoverBranch next = xoverComputeBranch(freq, type, sampleRate);
  // update() reads the branch from the audio interrupt
  AudioNoInterrupts();
  target = next;
  states[0] = {0.0f, 0.0f};
  states[1] = {0.0f, 0.0f};
  AudioInterrupts();
}

void CrossoverFilter::setHighpass(float freq, CrossoverType type) {
  applyBranch(hp, hpState, freq, type);
}

void CrossoverFilter::setLowpass(float freq, CrossoverType type) {
  applyBranch(lp, lpState, freq, type);
}

void CrossoverFilter::update(void) {
  audio_block_t* block = receiveReadOnly();
  if (!block) return;

  if (hp.count == 0 && lp.count == 0) {
    transmit(block);
    release(block);
    return;
  }

  float buffer[AUDIO_BLOCK_SAMPLES];
  arm_q15_to_float(block->data, buffer, AUDIO_BLOCK_SAMPLES);
  release(block);

  for (int s = 0; s < hp.count; s++) {
    const XoverSection& c = hp.section[s];
    XoverSectionState& st = hpState[s];
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
      buffer[i] = xoverProcessHighpass(c, st, buffer[i]);
    }
  }
  for (int s = 0; s < lp.count; s++) {
    const XoverSection& c = lp.section[s];
    XoverSectionState& st = lpState[s];
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
      buffer[i] = xoverProcessLowpass(c, st, buffer[i]);
    }
  }

  audio_block_t* out = allocate();
  if (!out) return;
  arm_float_to_q15(buffer, out->data, AUDIO_BLOCK_SAMPLES);
  transmit(out);
  release(out);
}
