# Vybes Teensy core fork

Forked files from the `framework-arduinoteensy` package (Teensyduino core),
installed into `~/.platformio/packages/framework-arduinoteensy/cores/teensy4/`
by `../core_fork_sync.py`, which runs automatically before every `teensy41`
build (`extra_scripts` in `platformio.ini`). No manual step: a fresh checkout
patches the package on first build. The pristine file is kept next to the
patched one as `<name>.vybes-orig`.

## Why

The stock USB audio input (`AudioInputUSB`) buffers at most 2 audio blocks
(~5.8ms) between the USB interrupt and the audio update, targeting a
64-sample occupancy. Host packet delivery is bursty at millisecond scale, so
the buffer regularly overflowed (dropped samples) and drained (inserted
silence) - audible as occasional clicks during USB playback, regardless of
CPU load or sample-rate matching. Measured on hardware 2026-08-09: ~10
overruns + ~1 underrun per 20s window while streaming at 5% CPU.

## Changes in `usb_audio.cpp` (all marked `VYBES`)

1. **`usb_audio_rx_hook(const int16_t *lr, unsigned int frames)`** - weak
   `extern "C"` hook offered every incoming isochronous packet before any
   buffering. Return nonzero to consume the packet. `AsyncAudioInputUSB`
   (in `fir_filters/`) defines the strong version and owns rate matching via
   resampling; the feedback endpoint then keeps reporting nominal 44.1kHz.
2. **Deep receive queue** - 8-block ring targeting 3 blocks of occupancy
   (~8.7ms input latency, ~14.5ms burst absorption) instead of the stock
   2-block / 64-sample scheme. Queue depth is exported as
   `volatile uint8_t usb_audio_rx_queue_count` for monitoring.
3. **Damped feedback loop** - the stock proportional-only correction acts on
   a double-integrator plant (occupancy integrates rate error, the
   accumulator integrates the correction), i.e. an undamped oscillator that
   never settles. A derivative term (`err + 16*(err - prev_err)`) makes it
   near critically damped; the crude `+3500` underrun kick is removed.

## Updating the platform package

When the Teensy platform/package updates, `core_fork_sync.py` aborts the
build with a hash mismatch. Re-merge: diff `usb_audio.cpp` here against the
new core file (the `VYBES` markers delimit every change), port the changes,
then refresh the hash:

```bash
shasum ~/.platformio/packages/framework-arduinoteensy/cores/teensy4/usb_audio.cpp | cut -d' ' -f1 > Teensy/core_fork/usb_audio.cpp.upstream.sha1
```

(run before the sync script has patched the new package - i.e. right after
the package update, or against the `.vybes-orig` backup).
