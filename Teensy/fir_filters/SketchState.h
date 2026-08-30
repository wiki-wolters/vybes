#ifndef SKETCH_STATE_H
#define SKETCH_STATE_H

#include "PEQProcessor.h"        // PEQBand, MAX_PEQ_BANDS
#include "CrossoverMath.h"       // CrossoverType
#include "AudioFilterFIRFloat.h"

// The sketch-wide DSP state plus the objects and services other translation
// units reach into (FirFiles.cpp today). Everything declared extern here is
// defined in fir_filters.ino; this header is the sketch's exported surface,
// not a home for logic.

// Number of output channels (octal I2S). Must match NUM_OUTPUTS on the ESP.
#define NUM_OUTPUTS 8

// PEQ bands per output channel (MAX_OUTPUT_PEQ on the ESP). The shared input
// EQ keeps MAX_PEQ_BANDS (15) from PEQProcessor.h.
#define MAX_OUTPUT_PEQ 10

#define MAX_FILENAME_LEN 64 // Maximum length for FIR filenames

const int CURRENT_VERSION = 4;

// Per-output channel state. Defaults are silent (source gains 0) - the ESP
// pushes the full DSP state after the "boot" event, so nothing plays from
// stale defaults.
struct OutputState {
  float sourceLeft = 0.0f;   // L bus contribution (linear gain)
  float sourceRight = 0.0f;  // R bus contribution
  float gainDb = 0.0f;       // output gain in dB (-40..+10, clamped)
  bool mute = false;         // effective mute (the ESP folds 'enabled' in)
  bool invert = false;
  int delayUs = 0;

  float hpFreq = 0.0f;       // 0 = off
  CrossoverType hpType = CROSSOVER_LR4;
  float lpFreq = 0.0f;
  CrossoverType lpType = CROSSOVER_LR4;

  PEQBand peq[MAX_OUTPUT_PEQ];
  bool eqEnabled = true;     // PEQ bypass (bands are kept; see setOutputEqEnabled)

  char firFile[MAX_FILENAME_LEN] = "";
  uint16_t firTaps = 0;      // taps currently loaded (0 = none)

  float currentGain = 0.0f;  // smoothed amp gain actually applied
};

//Define a structure for holding state
struct State {
  int version = CURRENT_VERSION;

  // Input gains
  float gainBluetooth = 1.0;
  float gainOptical = 1.0;
  float gainUSB = 1.0;
  float gainGenerator = 1.0;
  float gainAnalog = 1.0;
  float gainPlayer = 1.0; // SD playback (aux input 2); setPlaybackGain

  // Master Volume
  float volume = 0.5; // User-set volume
  float targetVolume = 0.5; // Target volume for smoothing
  bool muted = false;
  float mutePercent = 100.0; // Mute volume reduction percentage

  // Preset-level master toggles
  bool inputEqEnabled = true;
  bool firEnabled = true;
  bool delaysEnabled = true;

  OutputState outputs[NUM_OUTPUTS];

  // Shared input EQ bands (left and right run the same curve)
  PEQBand inputEqBands[MAX_PEQ_BANDS];
};

extern State state;

// The eight per-output FIR filters (one per output channel; see the audio
// graph in fir_filters.ino).
extern AudioFilterFIRFloat firFilter[NUM_OUTPUTS];

// Shared sketch services, defined in fir_filters.ino:

// SD media check and re-mount (see the comment at its definition for why
// this exists and when it must NOT probe the card).
bool sdReady();

// Re-apply user delays plus FIR latency alignment (call after tap counts
// change).
void applyDelays();

// RAM2 heap and audio-block-pool stats, printed where the budget matters.
void printMemoryStats(const char* tag);

#endif // SKETCH_STATE_H
