# 8-Output Channel Architecture (Config V1)

Design agreed 2026-07-25. This replaces the fixed left/right/sub model with a
MiniDSP-Flex-style routing matrix and eight identical output channels, matching
the octal I2S output hardware. This is a clean break: new Teensy + ESP32-S3
hardware, no migration from the old 3-channel config. The new schema starts at
**version 1**, but the version field and a migration hook are kept from day one
so future schema changes can migrate in place.

Deferred (discussed, not in scope for V1):

- Multirate sub FIR (decimate /8, FIR at ~5.5kHz, interpolate back). Saves ~8x
  taps and ~64x CPU on sub channels; revisit when the tap pool gets tight.

## Signal flow

```
inputs (spdif/bt/usb/analog/gen)
  -> input mixers (existing)
  -> input EQ (shared L/R, preference curve + SPL sets — room/house correction)
  -> routing matrix (per-output source gains for L and R buses)
  -> 8x output channel: HP + LP crossover -> output PEQ -> FIR -> delay
     -> gain / invert / mute
  -> octal I2S (and SPDIF mirrors outputs 0/1)
```

The Teensy stays dumb and per-channel: it knows nothing about crossover
objects, templates, or linking. All linking/reference resolution happens on the
ESP; the Teensy receives only resolved per-channel values.

## Config schema (V1)

As implemented: the mock server (mock-server/server.js + templates.js) and the
contract suite (WebUI/tests/contract/api-contract.test.js) are the executable
spec for the ESP32-S3 firmware.

```jsonc
{
  "version": 1,
  "active_preset_index": 0,
  "presets": [
    {
      "name": "Default",
      "template": "2.1",              // template id, or "custom" once edited beyond it
      "crossovers": [                  // shared crossover points (max 4)
        { "id": "sub_xo", "freq": 80, "type": "LR4", "locked": false, "min": 40, "max": 500 }
      ],
      "inputEq": {
        "enabled": true,
        "sets": [ { "spl": 0, "points": [ { "freq": 1000, "gain": 0, "q": 1 } ] } ]
      },
      "outputs": [                     // always 8 entries
        {
          "label": "Left",             // user-editable, shown everywhere in the UI
          "enabled": true,             // unused channels render collapsed/grey
          "source": { "left": 1.0, "right": 0.0 },   // mono sub = 0.5 / 0.5
          "hp": { "mode": "xover", "xover": "sub_xo" }, // mode: off | xover | manual
          "lp": { "mode": "off" },     // manual mode carries freq + type (LR2|LR4|BW2);
                                       // "off" keeps a previous xover ref for re-enable
          "hpFloor": 0,                // safety floor in Hz, see Safety model
          "peq": [ { "freq": 1000, "gain": 0, "q": 1 } ],  // max 10 per output
          "fir": "",                   // filename; '' = none. Enable is preset-level
          "delayUs": 0,                // capped at 20000 (existing decision)
          "gainDb": 0.0,               // -40..+10 dB
          "invert": false,
          "mute": false
        }
      ],
      "delaysEnabled": false,          // preset-level master toggles (A/B comparison)
      "firEnabled": false
    }
  ]
  // global system state (volume, mute, input gains, tone/noise) unchanged
}
```

Template ids: `2.0`, `2.1` (default), `2.2`, `2way-sub`, `3way`, `3way-2sub`.
Structural edits (source mix, HP/LP sections, enabling outputs) flip
`template` to `"custom"`; tuning edits (gain, delay, PEQ, FIR, mute, labels)
keep it. GET /preset additionally reports `firPool: {used, total}`.

### Limits

| Constant | Value | Why |
|---|---|---|
| `NUM_OUTPUTS` | 8 | octal I2S |
| `MAX_CROSSOVER_POINTS` | 4 | 3-way + sub needs 3; one spare |
| `MAX_OUTPUT_PEQ` | 10 | per output; MiniDSP-class. 8x10 SVF bands ~= 8% CPU, no global pool needed |
| `MAX_INPUT_PEQ` | 15 | unchanged, per SPL set (`MAX_PEQ_SETS` = 3) |
| `FIR_TAP_POOL` | 12288 | shared across all outputs, allocated at load; UI shows used/total |
| `MAX_DELAY_US` | 20000 | per channel, existing AudioMemory constraint (re-verify pool size at 8 delays) |

## Crossover points and the safety model

Crossover points are first-class named objects. A channel's HP/LP either
references one (`"mode": "xover"`) or owns a literal value (`"mode":
"manual"`). Editing a crossover point updates every filter that references it
in one operation — this is what makes the "sub crossover" slider adjust the
sub's LP and the mains' HP together. No general-purpose variable/macro system;
shared crossover points plus channel link groups (later) cover the real cases.

**Locked crossovers.** A mis-drag on a mid or tweeter crossover while music is
playing can fry a driver, so crossover points carry a `locked` flag:

- `locked: false` (e.g. `sub_xo`): rendered as a slider, live-editable within
  its template-defined range. Same UX as today.
- `locked: true` (e.g. `mid_xo`, `twt_xo`): rendered **view-only**. Editing
  requires an explicit flow: Edit button -> type new value -> Save -> Confirm.
  These values are set once during system setup and rarely touched.

Enforcement is server-side, not just UI: the ESP rejects a change to a locked
crossover unless the request carries `confirm=true`, and there is no
websocket/live-drag path for locked points. The UI merely reflects the rule.

**HP floor.** Each output has an `hpFloor` (Hz, 0 = none). The ESP rejects any
change that would leave the channel's effective high-pass below the floor —
whether by editing a crossover point, switching HP to manual, or turning HP
off. Templates set floors for fragile drivers (e.g. tweeter channels get
`hpFloor` well below the expected crossover but far above DC). This also closes
the classic bypass hazard: a global "crossover enabled" toggle must not exist
in a multi-way system; bypass is per-section and floor-checked.

## Templates

A template is a **factory, not a mode**: applying one generates the outputs
array, labels, crossover points (with locked flags and slider ranges), source
mixes, and hpFloors. Everything remains editable afterward in the advanced
view. The preset records its template id so the UI knows which simple view to
render; edits the simple view can't express flip it to `"custom"`.

| Template | Outputs | Crossover points (locked?) |
|---|---|---|
| 2.0 Stereo | L, R | — |
| 2.1 (default) | L, R, Sub | sub_xo (slider) |
| 2.2 | L, R, Sub 1, Sub 2 | sub_xo (slider) |
| 2-way active + sub | L/R woofer, L/R tweeter, Sub | sub_xo (slider), twt_xo (locked) |
| 3-way active | L/R low, L/R mid, L/R high | mid_xo (locked), twt_xo (locked) |
| 3-way + 2 subs | all 8 | sub_xo (slider), mid_xo (locked), twt_xo (locked) |

The 2.1 template reproduces today's product exactly (home view: volume, sub
level, sub crossover slider), so the simple experience is unchanged by default.

## Serial protocol (channel-indexed)

Positional 3-channel commands are replaced with output-indexed ones. The ESP
resolves crossover references before sending; the Teensy only ever sees
per-channel numbers.

```
setOutputGain   <ch> <db>
setOutputMute   <ch> <0|1>
setOutputInvert <ch> <0|1>
setOutputSource <ch> <lGain> <rGain>
setOutputDelay  <ch> <us>
setOutputHp     <ch> <freq> <type>     # freq 0 = off (subject to hpFloor on ESP side)
setOutputLp     <ch> <freq> <type>
setOutputEq     <ch> <band> <freq> <q> <gain>
setFir          <ch> <file>            # bare "setFir <ch>" clears
setFirEnabled   <ch> <0|1>
setInputEq      <band> <freq> <q> <gain>   # shared input EQ, unchanged semantics
```

`teensy_protocol.h` stays pure C so the host-native round-trip tests keep
covering every command.

## Teensy firmware changes

- Named per-channel objects and patchcords collapse into arrays sized
  `NUM_OUTPUTS`, built in a loop: source mixer, biquad HP/LP cascade, PEQ, FIR,
  delay, output amp per channel.
- The fixed `AudioFilterStateVariable` crossover pairs are replaced by a
  per-channel biquad cascade (up to LR4 HP + LR4 LP = 4 biquads).
- Bypass moves inside each processing object (PEQ already has `setBypass`);
  the connect/disconnect patchcord-swapping arrays do not scale to 8 channels.
- FIR: `MAX_FIR_TAPS` per-channel cap becomes a shared pool (`FIR_TAP_POOL`
  taps, ~16 bytes/tap with fast convolution). Loads that exceed the remaining
  pool are rejected with a clear error the UI can surface.
- Benchmark 8 concurrent fast-convolution engines before trusting the pool
  number — current 3-channel builds are RAM-limited, but 8x FFT work is new.

## Web UI plan

Two-layer model: the **simple view** is template-driven and stays the default
experience (2.1 looks exactly like today); the **Channels view** is the
Flex-style advanced editor. Both edit the same preset — the simple view is a
projection of it, not a separate mode.

### Views

- **HomeView** (daily driver) — unchanged in role: presets, master volume,
  input sources, mute. The hardcoded Left/Right/Sub speaker toggles become
  template-derived mute groups (e.g. "Subs" toggles all sub-labeled outputs).
- **PresetEditorView** becomes tabbed: **Tuning** | **Channels**.
  - *Tuning* (simple view, per template): input EQ (existing EQSection with
    SPL sets), Crossovers card, speaker-group levels and delays, FIR per
    output. For the 2.1 template this is today's editor, nearly unchanged.
  - *Channels* (advanced): eight channel strips — editable label, source mix,
    HP/LP summary, output PEQ (opens ParametricEQ), FIR file select, delay,
    gain, invert, mute. Desktop: strip columns; mobile: stacked accordion
    cards (iPhone is a first-class target). Editing anything the template's
    simple view can't express flips the preset to "custom" (with a notice).

### Crossovers card and the locked-edit flow

One card lists the preset's crossover points:

- Unlocked (`sub_xo`): existing RangeSlider, live, within template range.
- Locked (`mid_xo`, `twt_xo`): read-only value + lock icon + Edit button.
  Edit opens a ModalDialog: numeric input -> Save -> confirmation step that
  shows old value, new value, and the list of affected channel filters
  ("HP on L Mid, R Mid; LP on L Low, R Low") -> Confirm sends the request
  with `confirm=true`. No slider is ever rendered for a locked point.
- ESP-side rejections (locked without confirm, hpFloor violations, FIR pool
  exhaustion) surface as inline errors on the control that caused them.

### New components

`ChannelStrip.vue`, `SourceMixInput.vue`, `CrossoverCard.vue`,
`LockedValueModal.vue`, `FirPoolBar.vue` (used/total taps, shown wherever FIR
files are picked), `TemplateSelect.vue` (in the new-preset modal). Everything
else reuses the existing kit; the FIR file dropdown keeps the current
missing-file and no-SD fallbacks from PresetEditorView.

### State

Introduce a single preset-config store (Pinia or a shared composable): with 8
channels, shared crossover points, and a global tap pool, cross-cutting state
no longer fits the current per-view fetch pattern. Websocket messages mutate
the store; views render from it. Keep the existing debounced-PUT pattern for
sliders.

### Build order

1. Mock server + api-client updated to the V1 schema (UI work proceeds
   without hardware).
2. Config store + PresetEditorView restructured to tabs; Tuning tab reaches
   parity with today's editor via the 2.1 template.
3. Crossovers card with the locked-edit flow.
4. Channels tab (strips, source mix, per-channel PEQ, FIR + pool bar).
5. Template picker on preset creation; custom-flip logic; HomeView mute
   groups.
6. Later: stereo link groups, crossover/PEQ response overlay on the RTA.

## Versioning

`version: 1` for this schema. Keep the load-time version check and a
`migrate_config(from_version)` hook wired from the start, even though V1 has
nothing to migrate — schema changes during iteration bump the version and add
a migration step rather than silently reinterpreting fields.
