#include "AudioFilterFIRFloat.h"
#include <Arduino.h>

// The engine's fixed block size must match the audio library's
static_assert(AUDIO_BLOCK_SAMPLES == FirEngine::BLOCK_SAMPLES,
              "FirEngine assumes 128-sample audio blocks");

// Default constructor implementation
AudioFilterFIRFloat::AudioFilterFIRFloat()
  : AudioStream(1, inputQueueArray),
    enabled(true),
    wasProcessing(false)
{
}

// setEnabled method implementation
void AudioFilterFIRFloat::setEnabled(bool enable) {
  __disable_irq();
  enabled = enable;
  __enable_irq();
}

// Takes effect at the next loadCoefficients() call
void AudioFilterFIRFloat::setFastConvolution(bool enable) {
  engine.setFastConvolution(enable);
}

// loadCoefficients method implementation
bool AudioFilterFIRFloat::loadCoefficients(const float* coeffs, uint16_t newNumTaps) {
  // Step 1: Allocate and build the new engine's buffers with interrupts enabled.
  if (!engine.buildPending(coeffs, newNumTaps)) {
    return false;
  }

  // Step 2: Atomically swap pointers and re-initialize the filter.
  __disable_irq();
  engine.swapPending();
  wasProcessing = false;
  __enable_irq();

  // Step 3: Free the old buffers with interrupts enabled.
  engine.freeRetired();

  return true;
}

// update() method implementation
void AudioFilterFIRFloat::update(void) {
  unsigned long start_us = micros();

  audio_block_t* block = receiveReadOnly(0);
  if (!block) {
    wasProcessing = false;
    return;
  }

  // If filter is disabled or not configured, pass data through unchanged
  if (!enabled || engine.taps() == 0) {
    wasProcessing = false;
    transmit(block);
    release(block);
    return;
  }

  audio_block_t* outBlock = allocate();
  if (!outBlock) {
    wasProcessing = false;
    release(block);
    return;
  }

  float inputF32[AUDIO_BLOCK_SAMPLES];
  float outputF32[AUDIO_BLOCK_SAMPLES];

  arm_q15_to_float(block->data, inputF32, AUDIO_BLOCK_SAMPLES);
  release(block);

  // After a gap (bypassed, upstream stalled, or fresh coefficients) the fast
  // engine's history is stale audio - restart it from silence.
  if (engine.fastLoaded() && !wasProcessing) {
    engine.resetHistory();
  }
  wasProcessing = true;

  // No locking needed here: update() runs in the audio interrupt, and
  // loadCoefficients() swaps engine buffers with interrupts disabled.
  //
  // No output scaling either: the filter output is the exact convolution of
  // the input with the loaded coefficients, so the active and bypassed paths
  // have identical gain.
  engine.processBlock(inputF32, outputF32);

  arm_float_to_q15(outputF32, outBlock->data, AUDIO_BLOCK_SAMPLES);

  transmit(outBlock);
  release(outBlock);

  unsigned long elapsed_us = micros() - start_us;
  if (elapsed_us > max_update_us) {
    max_update_us = elapsed_us;
  }
}
