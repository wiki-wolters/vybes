#ifndef COMPARE_MODE_H
#define COMPARE_MODE_H

// Comparison mode: honest A/B level matching for EQ toggles, FIR toggles
// and preset switches. While a web client holds the mode (keepalive-driven,
// like the RTA), every audible-state change recomputes the active state's
// pink-weighted loudness and trims the louder state down to the quietest
// state heard so far in the session ("setCompareTrim <centi-dB>", always
// <= 0 so the trim itself can never clip). Louder never wins an A/B on
// loudness alone; what's left to hear is the actual correction.
//
// The loudness model is analytic, not measured: per enabled+unmuted output,
// the input-EQ curve plus the output-EQ curve (both only while enabled),
// sampled at 100 log-spaced frequencies 20Hz-20kHz (log-uniform sampling of
// a dB curve IS pink weighting - pink noise has equal energy per octave),
// plus the output's gain, source-routing level, and the pink-weighted gain
// of its FIR filter (a flat scalar the Teensy reports per load). Crossovers
// and dynamics are not modeled - they partition or compress energy rather
// than shift overall level, and the goal is killing multi-dB bias, not
// 0.1 dB precision.

// From the websocket frame handler: a client refreshed "compare:keepalive".
void compareModeKeepalive();

// From the websocket frame handler: a client sent "compare:off" - drop the
// interest now instead of waiting out the keepalive timeout.
void compareModeRelease();

// From the main loop: activation/expiry, Teensy trim keepalive refresh.
void compareModeLoop();

// From any handler that changed what the active preset sounds like
// (EQ writes/toggles, FIR changes, output gain/mute/source, preset switch).
// Just sets a dirty flag - the loop task does the math - so it is safe to
// call from any task, with or without the config lock held.
void compareOnStateChanged();

// From teensy_comm: one "FIRGAIN <ch> <centi-dB>" line arrived.
void compareSetFirGain(int channel, int centiDb);

bool compareModeActive();

#endif // COMPARE_MODE_H
