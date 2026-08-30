#ifndef DELAY_PROBE_H
#define DELAY_PROBE_H

// Auto delay alignment probe: chirps leave one output at a time while the
// web UI records them on the phone mic and correlates arrival times.
// Protocol and chirp contract: teensy_protocol.h. PROBE lines go straight
// to the ESP link (Serial1), which relays them to the web UI as probeEvent
// websocket messages. The chirp schedule itself lives in ProbeSource
// (sample-clocked, ISR context); everything here is loop()-context only.

// "startDelayProbe <mask> <level>" - see teensy_protocol.h for the contract
void startDelayProbe(int mask, float levelPercent);

// Restore everything the probe touched and report why it ended. Idempotent;
// the amp targets revert through the normal ramp, so ending is click-free.
// A null message restores silently (the implicit clean restart).
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
