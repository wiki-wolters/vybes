#ifndef AUDIO_HOLD_H
#define AUDIO_HOLD_H

#include <stdint.h>

// Audio hold (see CMD_SET_CONFIG_HOLD in teensy_protocol.h): every output is
// silenced while any of three independent sources is asserted.
//
//  - bootHold starts true so a Teensy that reboots under a running ESP stays
//    silent until that ESP has pushed the whole preset, rather than briefly
//    playing boot defaults at full-range and 50% volume. It covers power-on
//    until the first sync completes.
//  - syncHoldDepth counts "setConfigHold 1"/"setConfigHold 0" pairs and
//    nests, so two overlapping syncs (preset button pressed twice, or a boot
//    event racing an API call) release only when the outer one finishes
//    rather than the inner one unmuting mid-config.
//  - firLoadHold covers a queued FIR load: the SD reads take seconds, and
//    playing the new preset's gains through the old preset's filters is
//    worse than staying silent. It is set and cleared around loadFirFiles()
//    in the same loop() pass, so the fail-safe below never touches it.
//
// Fail-safe: an ESP running firmware without setConfigHold, or a release
// lost to a UART glitch, must not mute the device forever. Releasing early
// is only ever as bad as the old behaviour; staying silent is worse.
//
// The idle window is measured from the last command received, NOT from when
// the hold went on: a full sync is hundreds of commands and the ESP drains
// its queue as fast as its own loop allows, so a fixed deadline can expire
// mid-sync and unmute the outputs one at a time as each setOutputSource
// lands - audibly staggering the channels. Idle time is the thing that
// actually means "no sync coming".
//
// Hardware-free (timestamps and the router's dispatch count come in as
// arguments) so the state machine runs host-side: test/test_audio_hold.
class AudioHold {
public:
  static constexpr uint32_t IDLE_TIMEOUT_MS = 3000;

  // Audio is silent while this is true (outputTargetGain returns 0).
  bool held() const { return bootHold || syncHoldDepth > 0 || firLoadHold; }

  // "setConfigHold 1": a sync begins. Restarts the fail-safe idle window so
  // a hold asserted long after boot still gets its full timeout.
  void beginSync(uint32_t nowMs) {
    syncHoldDepth++;
    idleSinceMs = nowMs;
  }

  // "setConfigHold 0": a sync finished. Also clears the boot hold whatever
  // the depth - a completed sync is what boot was waiting for.
  void endSync() {
    if (syncHoldDepth > 0) syncHoldDepth--;
    bootHold = false;
  }

  // Stay silent across a queued FIR load (set when the load is queued,
  // cleared once loadFirFiles() ran).
  void setFirLoadHold(bool loading) { firLoadHold = loading; }

  // Start the fail-safe idle window at now rather than at millis() 0, so it
  // measures the ESP's response to the boot event and not however long
  // setup() spent on the SD card.
  void armFailsafe(uint32_t nowMs) { idleSinceMs = nowMs; }

  // Fail-safe release; call every loop() pass with the router's dispatch
  // count. Any dispatched command restarts the idle window, so an
  // in-progress sync can take as long as it likes; once the link has been
  // quiet for IDLE_TIMEOUT_MS the boot/sync holds are dropped and audio
  // comes back with whatever state did arrive. Returns true when it
  // released the hold, so the caller can log the event.
  bool pollFailsafe(uint32_t dispatchCount, uint32_t nowMs) {
    if (!bootHold && syncHoldDepth == 0) return false;
    if (dispatchCount != lastDispatch) {
      lastDispatch = dispatchCount;
      idleSinceMs = nowMs;
      return false;
    }
    if (nowMs - idleSinceMs > IDLE_TIMEOUT_MS) {
      bootHold = false;
      syncHoldDepth = 0;
      return true;
    }
    return false;
  }

private:
  bool bootHold = true;
  int syncHoldDepth = 0;
  bool firLoadHold = false;
  uint32_t idleSinceMs = 0;   // last command seen while holding (or the arm)
  uint32_t lastDispatch = 0;  // router dispatch count at the last poll
};

#endif // AUDIO_HOLD_H
