#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <PsychicHttp.h>

// One websocket handler per listener: a handler's client list is only ever
// touched from its own server's httpd task (connects, disconnects, and
// broadcasts marshaled over via httpd_queue_work), which is what makes
// broadcasting from the loop task safe.
extern PsychicWebSocketHandler wsHttp;
#ifdef CONFIG_IDF_TARGET_ESP32S3
extern PsychicWebSocketHandler wsHttps;
#endif

void setupWebSocket();

// Queue a message to every connected client on every listener. Safe to call
// from any task; the send itself runs on each listener's own httpd task.
void broadcastWebSocket(const char* message);

// Forward one Teensy RTA frame (hex payload) to all websocket clients
void broadcastRtaFrame(const char* hexData);

// Forward one Teensy GRM (gain-reduction meter) frame to all clients
void broadcastGrmFrame(const char* hexData);

// Forward one Teensy VU (input level meter) frame to all clients
void broadcastVuFrame(const char* hexData);

// Forward one Teensy delay-probe line (payload after "PROBE ") to all
// clients as a probeEvent message
void broadcastProbeEvent(const char* line);

// Announce that an output's FIR filter failed to load (code: nosd, missing,
// poolfull, toobig, nomem).
void broadcastFirLoadError(const char* presetName, int output,
                           const char* code, const char* file);

// SD recorder/player updates (mirrored from the Teensy's REC lines):
// the full state snapshot, a one-shot error (code per teensy_protocol.h),
// a warning, and "the set of recordings changed - refetch the list".
struct RecorderState; // teensy_comm.h
void broadcastRecorderState(const RecorderState& state);
void broadcastRecorderError(const char* code, const char* file);
void broadcastRecorderWarning(const char* detail);
void broadcastRecordingsChanged();

// Tracks client interest in RTA frames and relays it to the Teensy.
// Call from loop().
void websocketLoop();

#endif
