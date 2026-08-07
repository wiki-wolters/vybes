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

#endif // API_PROBE_H
