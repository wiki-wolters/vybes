# Auto-FIR Contracts (Phase 0)

Status: **binding interface contracts** for the auto-FIR implementation slices,
2026-08-30. The rationale lives in `AUTO_FIR_DESIGN.md`; this document is what
each implementation slice is built and verified against. Changing a contract
here means re-agreeing it first — implementations conform to this file, not
the other way around.

Slices and file ownership (an implementer touches nothing outside its list):

| Slice | Scope | Files |
|---|---|---|
| A | FIR file upload/delete: HTTP + UART + SD | `ESP/esp-web-server/` (api_fir, teensy_comm, teensy_protocol.h, web_server), `Teensy/fir_filters/` (new FirUpload module + command router hookup), `Teensy/test/` protocol/router tests, `WebUI/tests/contract/` |
| B | Measurement + design math in the browser | `WebUI/src/sweep-math.js`, `WebUI/src/fir-design.js`, `WebUI/tests/unit/` (their tests) |
| C | Wizard UI (later phase) | `WebUI/src/` views/components/stores; consumes A's routes and B's modules |
| — | Teensy measurement sweep command | With A (same protocol files); spec below |

Shared constants (already in source, restated for reference): tap pool
`FIR_TAP_POOL` = 12288, charge quantum `FIR_POOL_CHARGE_QUANTUM` = 128,
`NUM_OUTPUTS` = 8, `FIR_FILENAME_LEN` = 63, UART lines ≤ `TEENSY_MSG_MAX` = 80
bytes at 115200 baud, device sample rate 44117.647 Hz (nominal 44100 in the
probe schedule contract).

---

## Slice A — FIR file upload and delete

### HTTP (ESP)

**`POST /fir/upload?name=<file>`** — body is the raw file bytes.

- `name` must pass `isValidFirFilename` (≤ 63 chars, no spaces/control chars)
  and end in `.bin`, `.wav`, or `.txt`. The wizard uploads `.bin` only (raw
  little-endian float32 taps, no header — exact `size/4` tap accounting).
- Size: 4 ≤ size ≤ 51,200 bytes; a `.bin` size must be a multiple of 4.
- The body is **streamed** to the UART in chunks as it arrives — the ESP never
  buffers the whole file (heap discipline; the /restore stack-size lesson
  applies: this handler declares its own httpd stack size ≥ 10240).
- Success: `200 {"name": "...", "size": N, "taps": N}` — taps as reported by
  the Teensy's `FIRPUT OK`. The ESP invalidates its cached SD file list before
  responding, so an immediate `GET /fir/files` sees the new file.
- Errors: `400` bad name/size, `409 {"error":"recording"}` while the recorder
  is busy (SD contention), `409 {"error":"busy"}` if another upload is in
  flight (one at a time), `502` with the Teensy's `FIRPUT ERR` reason if the
  device rejects it, `504` on UART timeout (no ACK for 5 s).

**`DELETE /fir/files?name=<file>`**

- `409 {"error":"referenced","presets":[...]}` if any preset's output
  references the name; otherwise relays `firDelete`, invalidates the cached
  list, returns `200 {"name":"..."}`. `404` if the device reports no such
  file. Same `409 recording` guard.

### UART grammar (ESP → Teensy; replies Teensy → ESP)

Line-based like every other command; every line ≤ 80 bytes.

```
firPutBegin <name> <size> <crc32>     ->  FIRPUT BEGIN <name>
                                       |  FIRPUT ERR <reason>
firPut <seq> <base64>                 ->  FIRPUT ACK <seq>      (every 16th line and the final line)
                                       |  FIRPUT ERR <reason>   (aborts the transfer)
firPutEnd                             ->  FIRPUT OK <name> <size> <taps>
                                       |  FIRPUT ERR <reason>
firPutAbort                           ->  FIRPUT STOP
firDelete <name>                      ->  FIRDEL OK <name> | FIRDEL ERR <reason>
```

- `seq` counts from 0, decimal. Base64 payload ≤ 60 chars (45 raw bytes), so
  a full-pool file is ~1,100 lines (~7 s at 115200 baud).
- Flow control: the ESP sends at most 16 lines beyond the last ACK'd `seq`.
- `crc32` is IEEE 802.3 (reflected, init `0xFFFFFFFF`, final XOR
  `0xFFFFFFFF`), 8 lowercase hex chars, computed over the raw file bytes.
- Teensy writes to `upload.tmp` on the SD, and on `firPutEnd` verifies byte
  count and CRC, removes any existing target, renames, then replies. A
  failed verify leaves the SD exactly as before (`upload.tmp` removed).
- `ERR` reasons (single tokens): `badName`, `badSize`, `badSeq` (gap or
  repeat), `badB64`, `crc`, `sd`, `noSpace`, `busy` (recording or another
  transfer active), `state` (firPut/firPutEnd without Begin).
- Uploading never touches running filters. Overwriting a name currently
  loaded by the active preset leaves the loaded coefficients stale until the
  next assignment or preset activation — the wizard uses fresh versioned
  names (`a<gen>-<label>.bin`, e.g. `a3-woofer-l.bin`) so this path is never
  exercised in practice.

### Tests slice A must ship (parity on both sides — the standing trap)

- Teensy native: command-router/protocol tests for the full grammar —
  happy path, seq gap, CRC mismatch, oversize, abort, mid-transfer `busy`,
  and that `FIRPUT`/`FIRDEL` reply lines parse under the same rules as the
  existing reply grammar.
- ESP side: the protocol constants and framing land in `teensy_protocol.h`
  so the existing parity checks cover them.
- WebUI contract test (`tests/contract`, device-gated like the rest): upload
  a 512-tap `.bin` → `GET /fir/files` lists it with taps = 512 → pool math
  charges 512 → delete → gone; delete-while-referenced returns 409.

---

## Teensy measurement sweep command (with slice A)

The delay probe's machinery is reused (ProbeSource waveform recurrence,
per-output soloing, PROBE event relay), parameterized for measurement:

```
startSweepProbe <mask> <level%> <f0> <f1> <chirpSamples> <nPasses>
```

Replies (relayed to the web UI like probeEvent):

```
SWEEP START <mask> <nPasses> <preRoll> <spacing> <chirpSamples> <f0> <f1> <fade>
SWEEP CHIRP <slot> <ch>
SWEEP WARN unrouted <ch>
SWEEP DONE | SWEEP STOP | SWEEP ERR <reason>
```

- Waveform: the same double-precision log-sweep recurrence as ProbeSource,
  raised-cosine fades of `<fade>` samples each end, so the browser reference
  stays analytically exact.
- Slot order: outputs ascending, then the same list repeated `nPasses` times
  (**not** reversed like the delay probe — pass-to-pass comparison of the
  *same* output is the consistency/drift check).
- `spacing ≥ chirpSamples + 66150` (≥ 1.5 s IR tail at device rate).
- Defaults the wizard requests: f0 = 20 Hz, f1 = 20000 Hz, chirpSamples =
  131072 (~3 s), nPasses = 2.
- The values echoed in `SWEEP START` are the single source of truth for the
  JS reference generator — the browser never assumes compiled-in constants.
- The Teensy changes nothing about preset state: the **wizard** turns
  `firEnabled` and `inputEq.enabled` off for the session via the existing
  APIs and restores them after (input EQ off is an amendment to
  AUTO_FIR_DESIGN's stage 1: it is room-band correction that re-runs after
  the wizard, so it must not be baked into the driver measurement; output
  PEQ and crossovers stay active as designed).

---

## Slice B — measurement + design math

Interfaces are pinned by the stubs in `WebUI/src/sweep-math.js` and
`WebUI/src/fir-design.js` — JSDoc there is normative (units, array layouts,
zero-time conventions). Acceptance is the currently-skipped suite
`WebUI/tests/unit/fir-design-acceptance.test.js`, which runs against the
synthetic-system simulator in `WebUI/tests/helpers/synth-system.js`.
**The simulator and acceptance thresholds are verification harness — slice B
does not modify them.** If a threshold is genuinely wrong, that's a contract
change (top of this file).

Numbered acceptance criteria (mirrored in the test file):

1. **Deconvolution**: a simulated capture of a pure 5 ms delay recovers an IR
   peak at 5 ms ± 0.1 ms, ≥ 40 dB above the off-peak background.
2. **Drift**: an injected 80 ppm clock offset is estimated within ± 5 ppm
   from the two-pass spacing, and resampling by the estimate re-aligns the
   passes to within 1 sample.
3. **Magnitude correction**: on a synthetic system with two resonances
   (+6 dB @ 400 Hz Q 2, −4 dB @ 2.5 kHz Q 3) inside a 300 Hz–8 kHz band, the
   corrected system (kernel ⊛ system, computed by the test's own FFT) is flat
   within ± 1 dB across the band; the kernel's own gain outside the band
   stays within ± 1.5 dB of unity.
4. **Minimum-phase render** (budget 0): ≥ 50% of kernel energy in the first
   64 samples, and corrected-system group delay ≤ 1 ms across the band.
5. **Latency budget**: with budget B samples, the kernel's bulk lead is
   exactly B ± 2 samples, and excess-phase deviation of the corrected system
   is reduced ≥ 70% relative to uncorrected over the band the budget reaches
   (f ≥ 1/(B/rate)).
6. **Presence cap**: a +6 dB synthetic error at 2 kHz with a ± 3 dB cap in
   1–5 kHz yields a kernel applying no more than 3 dB of cut there.
7. **Tap budget**: on the design doc's worked topology (3-way stereo +
   2 subs), the plan zeroes subs and disabled outputs, gives every corrected
   output ≥ 256 taps in multiples of 128, allocates symmetrically for
   symmetric passbands and monotonically in 1/fLo (woofer/mid ratio in
   [2, 4.5]), and fills the pool to within 6 quanta of pool − 2×128 reserve
   without exceeding it. (The design doc's 512/1,536/3,584 illustration is
   one valid plan, not a pinned answer.) Suggested algorithm: the largest
   common support factor c such that Σ quantize₁₂₈(clamp(c·rate/fLo, 256,
   8192)) fits the budget — but the test constrains outcomes, not the
   algorithm.
8. **Encoding**: `encodeFirBin` round-trips: bytes/4 taps, little-endian
   float32, exact values.

Notes for the implementer: the radix-2 FFT in `delay-align.js` may be
extracted into a shared module (that refactor is in-scope for B; keep
`delay-align.js` tests green). Everything must be pure functions on typed
arrays — no DOM, no Web Audio — so it all runs under vitest/node.

---

## Slice C — wizard UI (contract sketch; finalized when B lands)

Stages per AUTO_FIR_DESIGN §User workflow. Hard requirements: budget screen
before any audio (uses `planTapBudget` + `GET /preset/fir/pool`); measurement
uses `startSweepProbe` and captures in one getUserMedia session; preset flags
(`firEnabled`, `inputEq.enabled`) saved/restored around the session; apply
path = copy preset → `POST /fir/upload` per output → `PUT /preset/output/fir`
→ delays via existing route; every capture reuses the analyzer's capture-chain
readback and cal-profile plumbing. Development against mock-server +
stubbed getUserMedia (the established trick).

---

## Acceptance gates (run by the integrator, not the slices)

1. `pio test -e native` — full suite, both firmwares build (`teensy41`,
   `bench`, ESP envs).
2. `npm test` in WebUI — including the acceptance suite once B lands
   (the skips get removed as part of B's delivery).
3. `git status` scoped per slice — no files outside the ownership table.
4. Contract tests against real hardware before any flash is called done.
