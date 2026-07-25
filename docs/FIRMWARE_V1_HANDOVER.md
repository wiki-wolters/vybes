# Firmware V1 Handover: ESP32-S3 + Teensy work

Status as of 2026-07-26: the WebUI, mock server and API contract for the
8-output V1 architecture (docs/CHANNEL_ARCHITECTURE.md) are **done and
verified**. The **ESP32-S3 work below is implemented** (compiles for
esp32s3 + esp32dev; contract suite passes against the mock but has not yet
been run against hardware). The Teensy firmware (Teensy/fir_filters) still
implements the old 3-channel (left/right/sub) model - the remaining work
order is the Teensy section, plus a hardware contract run.

## Sources of truth (in priority order)

1. **WebUI/tests/contract/api-contract.test.js** — the executable API spec.
   It runs hermetically against the mock (`npm run test:contract` in WebUI/)
   and against a real device (`VYBES_API_URL=http://vybes.local npm run
   test:contract`). The ESP implementation is finished when the suite passes
   against hardware. Every endpoint, status code, clamp/reject behavior and
   websocket broadcast shape is asserted there.
2. **mock-server/server.js + mock-server/templates.js** — reference
   implementation. Template definitions (outputs, crossover points, locked
   flags, min/max ranges, hpFloors) port from templates.js verbatim.
3. **docs/CHANNEL_ARCHITECTURE.md** — design rationale, config schema,
   safety model, Teensy graph plan.

## Constants (must match everywhere)

| Constant | Value |
|---|---|
| NUM_OUTPUTS | 8 |
| MAX_CROSSOVER_POINTS | 4 |
| MAX_OUTPUT_PEQ | 10 per output |
| MAX_INPUT_PEQ | 15 per SPL set (MAX_PEQ_SETS 3, unchanged) |
| FIR_TAP_POOL | 12288 taps shared across outputs |
| Output gain | -40..+10 dB (clamped) |
| Delay | 0..20000 us per output (rejected outside) |
| Crossover types | LR2, LR4, BW2 |
| Templates | 2.0, 2.1 (default), 2.2, 2way-sub, 3way, 3way-2sub |

## ESP32-S3 work (DONE 2026-07-26)

Decisions made during implementation that the Teensy work must honor:

- **FILES listing carries sizes**: the `getFiles` reply lines become
  `"name size"` (bytes). The ESP parses the optional second token
  (teensy_comm.cpp getCachedFirFileSize) and degrades gracefully to
  name-only lines from older firmware (files then count as the default
  tap estimate).
- **Tap estimation on the ESP** (api_fir.cpp): `.bin` = size/4 (raw
  float32 taps), any text format = size/12 (~bytes per coefficient line),
  unknown/unsized file = flat 2048 (matches the mock). The Teensy's
  load-time pool check remains the authoritative backstop.
- **Disabled output = muted**: the ESP sends `setOutputMute <ch> 1` when
  an output is disabled (effective mute = mute || !enabled); the Teensy
  needs no "enabled" concept.
- **Legacy CMD_\* defines** (setDelays, setEq, setCrossoverFrequency, …)
  are still in teensy_protocol.h only because Teensy/test/test_protocol
  references them. Delete them when the Teensy protocol + tests move to
  the setOutput* commands - the round-trip test asserts the two command
  tables cover each other, so both sides change together.
- ESP constants: MAX_PRESETS 12 and PRESET_NAME_MAX_LEN 48 (the contract
  suite generates ~41-char names and holds ~8 presets concurrently);
  Teensy command queue QUEUE_SIZE 200 (a full V1 sync is ~180 commands).
- Hardware contract runs: the FIR-pool tests only pass if the SD card
  carries files whose derived tap counts match the mock's map
  (fir_room1/2 = 4096 taps, fir_speaker1/2 = 2048, fir_flat = 1024).

- **config.h/cpp rework**: replace the left/right/sub structs with
  `Output outputs[NUM_OUTPUTS]`, `CrossoverPoint crossovers[4]`, inputEq
  (rename of preference_curve sets), per-preset `template`, `delaysEnabled`,
  `firEnabled`. Schema starts at CONFIG_CURRENT_VERSION 1 for the new
  hardware, but keep the version field + a migrate hook wired from day one.
- **Template factory**: port templates.js (labels, sources, xover refs,
  locked flags, ranges, hpFloors). POST /preset?action=create&template=…
- **New/changed endpoints** (see contract suite for exact shapes):
  - GET /templates, GET /preset (V1 shape + firPool), GET /preset/fir/pool
  - PUT /preset/crossover?id=&frequency=&confirm= and
    /preset/crossover/enabled?id=&enabled=&confirm=
  - PUT /preset/output/{label,enabled,source,gain,mute,invert,delay,filter,
    eq,eq/point,fir}?output=0..7
  - Kept: /preset/eq* (input EQ), /preset/delay/enabled, /preset/fir/enabled,
    preset CRUD, all system endpoints (/status, /volume, /mute*, /gains/input,
    generator, RTA).
  - Removed (no longer served): /preset/delay?speaker=, /preset/fir?speaker=,
    /preset/gains, /gains/speaker usage by the UI (endpoint may stay for
    remote/button code until that is reworked).
- **Safety enforcement lives on the ESP** (the UI only mirrors it):
  - Locked crossover point writes without confirm=true → 409 with
    `{"locked": true}`.
  - Any change leaving an enabled output's effective HP below its hpFloor →
    409, including crossover bypass and per-output filter edits, regardless
    of confirm.
  - FIR loads exceeding the tap pool → 409 with `{used, total}`. Tap counts
    per file come from Teensy-reported file sizes (decided: see the
    "name size" listing + tap estimation notes above).
  - Structural edits (source, hp/lp, output enabled) flip template→"custom"
    and report `"template": "custom"` in the outputChanged payload once.
- **Websocket broadcasts**: outputChanged {output, changes, firPool?,
  template?}, outputEqChanged, crossoverChanged {id, crossoverFreq},
  crossoverEnabledChanged {id, crossoverEnabled}; legacy delayChanged /
  firChanged / eq shapes for input EQ unchanged.
- **Teensy sync**: updateTeensyWithActivePresetParameters resolves crossover
  references to concrete per-channel frequencies before sending - the Teensy
  never sees crossover ids or templates.

## Teensy work

- **Serial protocol** (teensy_protocol.h stays pure C; keep the host-native
  round-trip tests): replace 3-channel commands with output-indexed ones:
  `setOutputGain <ch> <dB>`, `setOutputMute <ch> <0|1>`, `setOutputInvert`,
  `setOutputSource <ch> <l> <r>`, `setOutputDelay <ch> <us>`,
  `setOutputHp/Lp <ch> <freq|0> <LR2|LR4|BW2>`, `setOutputEq <ch> <band>
  <f> <q> <g>`, `setFir <ch> [file]`, `setFirEnabled <0|1>`,
  `setInputEq <band> <f> <q> <g>` (shared input EQ), `setDelaysEnabled`.
- **Audio graph** (fir_filters.ino): collapse the ~90 named objects into
  arrays sized NUM_OUTPUTS built in a loop: per output a source AudioMixer4
  (inputs: L bus, R bus post input-EQ), biquad cascade for HP+LP (up to LR4
  each = 4 biquads), PEQProcessor (10 bands), AudioFilterFIRFloat,
  AudioEffectDelay, output amp (gain/invert/mute). Octal I2S channels 0-7
  map 1:1; SPDIF out mirrors outputs 0 and 1.
- **Bypass moves inside the processing objects** (PEQProcessor already has
  setBypass; give FIR/delay the same) - the patchcord connect/disconnect
  arrays don't scale to 8 channels.
- **FIR tap pool**: replace the per-channel MAX_FIR_TAPS cap with a shared
  12288-tap pool allocated at load; reject loads that exceed it with an
  error the ESP can relay.
- **Input EQ stays shared** on the L/R buses ahead of the routing matrix
  (peqLeft/peqRight, 15 bands, SPL sets) - unchanged behavior.
- **Benchmarks before trusting the numbers** (flagged in the design doc):
  AudioMemory sizing with 8 delay lines at the 20 ms cap, and CPU with 8
  concurrent fast-convolution engines (current builds only ever ran 3).

## Suggested order

1. ESP config structs + template factory + GET endpoints; run the contract
   suite against the device early and often (it creates/deletes its own
   contract-test-* presets and restores globals).
2. ESP write endpoints + safety enforcement + broadcasts (suite green).
3. Teensy protocol + graph arrays behind the existing 3 outputs first
   (channels 3-7 silent), then full 8-channel bring-up.
4. FIR pool + benchmarks.

## Loose ends (UI side, for context)

- Stereo link groups and a crossover/PEQ response overlay on the RTA are
  designed but deliberately deferred (stage 6+ in CHANNEL_ARCHITECTURE.md).
- The WebUI work is uncommitted on main as of this writing.
- mock-server keeps /gains/speaker and speakerGains in /status for the old
  firmware's sake; the UI no longer calls them.
