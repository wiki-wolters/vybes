# Auto-FIR: Hardware Session Handover

Written 2026-08-30. The auto-FIR feature is software-complete and committed;
everything below is what the first hardware session needs. Background:
`AUTO_FIR_DESIGN.md` (rationale), `AUTO_FIR_CONTRACTS.md` (wire grammar,
acceptance criteria).

Commits, in order: `7df925f` contracts + acceptance harness → `5aff082`
measurement math (sweep-math.js) → `abecf7a` designKernel → `675b33f`
firmware upload path + sweep command → `9614c0c` sweep HTTP routes →
`2b6e838` the wizard. All verified against the native test suite (175),
the WebUI suite (191), and the mock server — **nothing has touched real
hardware yet**.

## Pre-flight

1. **Back up the config**: download via the Home view's backup button (or
   `GET /backup`) to `ESP/config-backup-<date>-preflash.msgpack` — the
   uploadfs step below wipes it.
2. `WebUI/dist` is current as of 2b6e838 (`npm run build` if in doubt);
   `ESP/esp-web-server/data/dist` is a symlink to it. `data/certs/` holds
   the good mkcert pair from the 2026-08-13 cert resolution — **this Mac's
   data dir is safe to flash** (the phone-trust CA matches).

## Flash sequence

```
pio run -d ESP -e esp32s3 -t uploadfs --upload-port <CH343 port>   # wipes config
pio run -d ESP -e esp32s3 -t upload   --upload-port <CH343 port>
pio run -d Teensy -e teensy41 -t upload
```

- Explicit `--upload-port` always: autodetect can grab the Teensy instead
  of the CH343.
- Opening the CH343 port resets the S3 — check ping/mDNS/curl before any
  serial poking, or the evidence is gone.
- Restore the config (`POST /restore` or the UI button) after uploadfs.
- Known lie: the FIRST `/fir/files` and `/recorder` calls after boot serve
  stale cache defaults — retry once before believing them.

## Test ladder (in order — each rung assumes the previous held)

1. **Boot sanity**: web UI loads over HTTPS from the phone, presets play.
2. **Upload contract tests** (from `WebUI/`):
   ```
   VYBES_API_URL=https://vybes.local npm run test:contract
   ```
   Three new device-gated tests: upload a 512-tap `.bin` → listed with
   exact taps → pool charges 512 → delete; delete-while-referenced → 409;
   bad name / missing file. This exercises the whole HTTP→UART→SD chain.
3. **Sweep smoke test** (short, before trusting a full session):
   ```
   curl -k -X PUT "https://vybes.local/probe/sweep/start?chirpSamples=16384&passes=1&level=30"
   ```
   Expect a `SWEEP START ...` probeEvent on the websocket with the derived
   schedule, one `SWEEP CHIRP` per enabled output, audible sweeps soloing
   each output, then `SWEEP DONE` and normal audio back (click-free ramp).
   `PUT /probe/sweep/stop` must cut it off mid-run.
4. **The wizard**, at the listening seat: FIR section of the active preset →
   "Auto-measure & correct with phone mic…". iPhone: set Control Center
   **Mic Mode → Wide Spectrum** first; HTTPS is required for the mic.
   Defaults are ~3 s sweeps at ~4.5 s spacing — roughly a minute of
   continuous capture for a 6-output rig at 2 passes. Apply lands on a
   preset copy, so the original is untouched.

## Watch items (untested against real hardware)

- **UART under load**: the 16-line ACK window and 5 s per-op timeouts have
  only run against the native fake. A full-pool upload is ~7 s of solid
  UART traffic sharing the line with keepalives/RTA frames — watch for
  `504` (timeout) or `FIRPUT ERR badSeq` (desync), and grab serial logs if
  either appears.
- **ESP heap during upload**: the body stages to a LittleFS temp file while
  a TLS socket sits open; heap-wise this is designed to be flat, but watch
  `minFreeInternal` telemetry during rung 2 anyway.
- **The httpd task blocks** for the duration of each upload — with the
  2-socket TLS ceiling the UI may feel stalled during Apply. Cosmetic, but
  worth knowing before diagnosing it as a hang.
- **Teensy replies moved to `printf`** in slice A (ITCM budget fix) — the
  reply formatting has passed native tests but not real Serial1.
- **Arrival detection windows**: drift estimation searches ±20 ms around
  each nominal slot; physical inter-output path differences beyond that
  (very large rooms / distributed subs) would break arrival matching. The
  wizard's review stage flags low-SNR outputs — believe it.
- **RAM watermarks**: `teensy_size` was clean (RAM2 free-for-malloc
  113,248, unchanged), but check the boot heap telemetry once FIRs load.

## Rollback

The wizard's Apply writes only to a preset copy — deleting that preset and
its `a<gen>-*.bin` files (`DELETE /fir/files?name=`) restores the previous
state exactly. Firmware rollback is `git checkout 3f6d472 -- Teensy ESP`
territory plus reflash; the config backup from pre-flight covers the rest.

## If something needs code changes

The mock server reproduces the whole flow without hardware
(`mock-server` + the `webui-dev-c` launch config); the acceptance suite
(`WebUI/tests/unit/fir-design-acceptance.test.js`) and the wizard's
synthetic E2E test (`fir-wizard.test.js`) are the regression net — keep
them green. The wire grammar is contract-bound: change
`AUTO_FIR_CONTRACTS.md` first, then both sides plus `test_protocol`, never
one side alone.
