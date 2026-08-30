#ifndef DELAY_PROBE_H
#define DELAY_PROBE_H

#include <Arduino.h>
#include "OutputStream.h"

// Auto delay alignment probe: chirps leave one output at a time while the
// web UI records them on the phone mic and correlates arrival times.
// Protocol and chirp contract: teensy_protocol.h. PROBE lines go straight
// to the ESP link (Serial1), which relays them to the web UI as probeEvent
// websocket messages. The chirp schedule itself lives in ProbeSource
// (sample-clocked, ISR context); everything here is loop()-context only.
//
// The measurement sweep probe (docs/AUTO_FIR_CONTRACTS.md) reuses this same
// machinery - waveform generator, output soloing, keepalive/cleanup - so it
// lives here too. The two are mutually exclusive (one shared ProbeSource):
// starting either kind while the other is running implicitly cleans it up
// first, exactly like restarting the same kind already did.

// "startDelayProbe <mask> <level>" - see teensy_protocol.h for the contract
void startDelayProbe(int mask, float levelPercent);

// "startSweepProbe <mask> <level%> <f0> <f1> <chirpSamples> <nPasses>" - see
// teensy_protocol.h for the contract. Slot order is masked outputs
// ascending, repeated nPasses times (not reversed like the delay probe).
void startSweepProbe(int mask, float levelPercent, double f0Hz, double f1Hz,
                     uint32_t chirpSamples, int nPasses);

// TEENSY_COMMAND_LIST handler for startSweepProbe.
void handleStartSweepProbe(const String& command, String* args, int argCount, OutputStream& stream);

// Restore everything the probe touched and report why it ended. Idempotent;
// the amp targets revert through the normal ramp, so ending is click-free.
// A null message restores silently (the implicit clean restart). Existing
// callers pass a literal "PROBE ..." message; if a sweep was actually the
// thing running, its "PROBE " prefix is swapped for "SWEEP " so callers
// that only know about the delay probe still report the right kind.
void probeCleanup(const char* message);

// Track the chirp schedule; call every loop() pass.
void probeLoop();

bool probeIsActive();

// While the probe runs, the amp gain output ch should settle at: the fixed
// probe level for the soloed output, 0 for every other output. The caller
// applies its own invert sign (invert is kept - the UI correlates on
// magnitude).
float probeGainForOutput(int ch);

#endif // DELAY_PROBE_H
