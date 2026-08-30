#ifndef API_PROBE_H
#define API_PROBE_H

#include <PsychicHttp.h>

// Auto delay alignment probe (see teensy_protocol.h for the wire contract).
// PUT /probe/delay/start?level=<0-100>  - chirp every enabled output of the
//   active preset; replies with the chirp schedule the web UI correlates
//   against. Progress arrives as probeEvent websocket messages relayed from
//   the Teensy's PROBE lines.
// PUT /probe/delay/stop                 - cancel a running probe.
esp_err_t handlePutProbeDelayStart(PsychicRequest *request);
esp_err_t handlePutProbeDelayStop(PsychicRequest *request);

// Measurement sweep session (auto-FIR wizard; docs/AUTO_FIR_CONTRACTS.md).
// PUT /probe/sweep/start?level=..&f0=..&f1=..&chirpSamples=..&passes=..
//   - sweep every enabled output of the active preset, ascending, repeated
//   `passes` times. Unlike the delay probe, the schedule (preRoll/spacing/
//   fade) is derived on the Teensy, so the authoritative numbers arrive in
//   the SWEEP START probeEvent websocket message - this response only
//   echoes the request and the output order.
// PUT /probe/sweep/stop - cancel (stops either kind of probe session).
esp_err_t handlePutProbeSweepStart(PsychicRequest *request);
esp_err_t handlePutProbeSweepStop(PsychicRequest *request);

#endif // API_PROBE_H
