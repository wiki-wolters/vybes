#ifndef TELEMETRY_STREAMS_H
#define TELEMETRY_STREAMS_H

// The three keepalive-gated telemetry streams the ESP relays to the web UI:
// RTA spectrum frames, GRM compressor gain-reduction frames, and VU input
// peak frames. Command handlers call the set* entry points (every such
// command is also the keepalive); loop() calls the *Loop pacers. Frame
// formats are documented at each sender in TelemetryStreams.cpp.

// Fill the RTA band-center table; call once in setup().
void telemetryBegin();

void setRtaEnabled(bool enabled);
void setGrmEnabled(bool enabled);
void setVuEnabled(bool enabled);

// Whether RTA frames are streaming - read by updateRtaSource() in the
// sketch, which routes the FFT tap (graph wiring stays with the graph).
bool rtaStreaming();

void rtaLoop();
void grmLoop();
void vuLoop();

#endif // TELEMETRY_STREAMS_H
