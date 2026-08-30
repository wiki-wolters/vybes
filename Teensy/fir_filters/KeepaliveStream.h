#ifndef KEEPALIVE_STREAM_H
#define KEEPALIVE_STREAM_H

#include <stdint.h>

// Keepalive-gated frame pacing, shared by the telemetry streamers (RTA, GRM,
// VU): the ESP refreshes "set<X> 1" every couple of seconds while a web
// client is watching, so streaming stops on its own when the keepalives
// stop - a dropped connection can't leave the Teensy streaming forever.
//
// Per loop() pass:
//   if (!s.enabled()) return;
//   if (s.keepaliveExpired(now)) { /* stop path */ return; }
//   if (!s.frameDue(now)) return;
//   ...build the frame...
//   if (the UART TX buffer is busy) return; // skip; the next frame is fresher
//   ...write...
//   s.markFrameSent(now);
//
// keepaliveExpired() doesn't disable the stream itself because stopping can
// have side effects (the RTA re-routes its FFT tap and logs); the caller
// runs its own stop path - setEnabled(false, ...) or disable().
//
// Hardware-free (timestamps come in as arguments) so the pacing runs
// host-side: test/test_keepalive_stream.
class KeepaliveStream {
public:
  KeepaliveStream(uint32_t frameIntervalMs, uint32_t keepaliveTimeoutMs)
    : intervalMs(frameIntervalMs), timeoutMs(keepaliveTimeoutMs) {}

  bool enabled() const { return on; }

  // "set<X> <0|1>": every such command is also a keepalive. Returns true
  // when the enabled state actually changed (an edge), so callers with side
  // effects can act on the change only.
  bool setEnabled(bool enable, uint32_t nowMs) {
    lastKeepaliveMs = nowMs;
    if (enable == on) return false;
    on = enable;
    return true;
  }

  void disable() { on = false; }

  // The keepalives have gone stale (only ever true while enabled).
  bool keepaliveExpired(uint32_t nowMs) const {
    return on && nowMs - lastKeepaliveMs > timeoutMs;
  }

  // The frame interval has elapsed since the last sent frame.
  bool frameDue(uint32_t nowMs) const {
    return on && nowMs - lastFrameMs >= intervalMs;
  }

  void markFrameSent(uint32_t nowMs) { lastFrameMs = nowMs; }

private:
  const uint32_t intervalMs;
  const uint32_t timeoutMs;
  bool on = false;
  uint32_t lastKeepaliveMs = 0;
  uint32_t lastFrameMs = 0;
};

#endif // KEEPALIVE_STREAM_H
