# Dynamic EQ

The shared input EQ (the "preference curve" on the L/R buses, ahead of the routing
matrix) used to be one fixed curve. It is now a curve that follows the volume:
two anchors above, an automatic loudness compensation below.

This document is the contract. The ESP web server, the mock server, the WebUI and
the Teensy firmware all implement the same rules, and the contract suite
(`WebUI/tests/contract/api-contract.test.js`) enforces the HTTP half of them.

## Why

A tuning only sounds right at the level it was made at. Turn it up and the bass a
listener asked for turns into boom; turn it down and the same bass disappears —
the ear's low-frequency sensitivity falls faster than its midrange sensitivity as
level drops (ISO 226 equal-loudness contours). A single stored curve is therefore
a curve for one volume.

The two halves of the problem are not the same problem, so they get different
mechanisms:

* **Above the reference volume** the correction is *taste*, not physics: how much
  less bass a particular system wants when it is playing loud is a property of
  the room, the speakers and the listener. So it is user-configured — a second
  set of gains, and the device crossfades between them.
* **Below the reference volume** the correction *is* physics, and the contours
  say what it should be. So there is nothing to configure: one on/off switch.

## The two anchors

`InputEq` holds up to two band sets, tagged by role:

| role | `spl` tag | meaning |
| --- | --- | --- |
| reference | `0` | what the preset sounds like at `referenceVolume` |
| loud | `1` | what it should sound like at `loudVolume` |
| unused | `-1` | free slot |

(`spl` was a never-implemented "EQ per SPL" field. It is a role tag now; the JSON
key keeps its old name so older backups parse without a rewrite.)

**Band count, frequencies and Qs are shared between the anchors. Only the gains
differ.** That is what makes band-for-band interpolation meaningful at all — two
independently-shaped curves have no correspondence to crossfade along. The ESP
enforces the invariant on every write:

* an edit to a reference point writes its frequency and Q through to the loud set;
* adding a reference band adds a flat one to the loud set;
* removing reference bands trims the loud set with them;
* a write to the loud set carries **gains only**.

`loudVolume == 0` means "no loud anchor": the reference curve plays at every
volume at or above the reference.

## The volume law

The slider is a percent; the Teensy cubes the linear value, so a percent is

```
dB = 60 · log10(pct / 100)          floored at −60 dB
```

| slider | dB | | slider | dB |
| --- | --- | --- | --- | --- |
| 100% | 0.0 | | 50% | −18.1 |
| 89% | −3.0 | | 35% | −27.4 |
| 79% | −6.1 | | 25% | −36.1 |
| 71% | −8.9 | | 16% | −47.8 |
| 63% | −12.0 | | ≤10% | −60.0 (floor) |

The floor keeps 0% finite; below 10% the interpolation and compensation are
already pinned at their extremes anyway.

`volumePctToDb()` in `WebUI/src/eq-math.js` is the reference implementation.

## Interpolation and hold

With `refDb`, `loudDb` and `volDb` the three volumes in dB:

```
t = clamp((volDb − refDb) / (loudDb − refDb), 0, 1)
gain[i] = refGain[i] + t · (loudGain[i] − refGain[i])
```

* At or below the reference anchor, and whenever there is no loud anchor, the
  reference gains play unchanged.
* Between the anchors the two curves crossfade band-for-band, linearly in dB.
* Above the loud anchor the loud curve **holds** — it is not extrapolated. A
  curve extrapolated past its anchor runs away: at 2× the anchor distance a
  −6 dB band becomes −12 dB, and nothing the user ever listened to justifies it.

`dynamicEqGains()` in `WebUI/src/eq-math.js` is the reference implementation.

## Loudness compensation

Below the reference anchor, with `drop = refDb − volDb` (a positive number of dB),
the device applies **two high-shelf filters**:

| corner | gain |
| --- | --- |
| 300 Hz | `−min(15, 0.25 · drop)` dB |
| 60 Hz | `−min(15, 0.25 · drop)` dB |

RBJ audio-EQ-cookbook high shelves with slope S = 1 (`Q = 1/√2`), at the device's
44.1 kHz — the digital shelf, so what the UI draws is what the Teensy runs.

**Two shelves, not one**: the ear's low-frequency sensitivity falls away in two
stages — gently below ~300 Hz, then much faster below ~60 Hz. A single shelf can
fit one of those or the other, not both. 0.25 dB of tilt per dB of level drop is
what matches the contours; the 15 dB cap per shelf stops a very low volume asking
for an extreme tilt (it binds at a 60 dB drop, beyond the useful range).

### Fit against ISO 226:2003

For a reference listening level of 80 phon and drops of 5…40 dB, the shelf pair
tracks the equal-loudness prediction

```
wanted(f) = Lp(f, R − drop) − Lp(f, R) + drop
```

to within **1.27 dB** at every ISO 226 band from 20 Hz to 2 kHz. The physics test
in `WebUI/tests/unit/eq-math.test.js` embeds the standard's `af`/`Lu`/`Tf` table
and asserts a 1.5 dB bound.

The fit is deliberately **low-frequency only**. Above ~2 kHz the contours' shape
depends on the absolute SPL at the listening position, which the device does not
know — it knows a slider percent, not a sound pressure level. Guessing there
would colour the top end on every quiet listen, for a correction that is at most
a dB or so.

### Why cuts, and what a user notices

The compensation is expressed as **cuts above the corners**, never as a boost
below them. A boost would ask for headroom the amplifier may not have, right at
the frequencies that consume it fastest, and would clip the very material it was
supposed to rescue.

The visible consequence: **below the reference volume the mids fall about 0.5 dB
for every dB the slider gives up** (two shelves × 0.25 dB/dB), on top of the
slider's own attenuation. Turning down by 10 dB drops the bass by 10 dB and the
midrange by 15 dB. That is the intended behaviour — the balance is what is being
preserved, not the absolute level — but it is why the device gets quiet faster
with loudness on, and why the switch exists.

The EQ graph draws the compensation **relative to 1 kHz**
(`loudnessCompensationRelDb()`), because on screen it reads as a bass lift, which
is what a listener hears.

## Config schema (v5)

`CONFIG_CURRENT_VERSION` is 5. `Preset.inputEq`:

```jsonc
"inputEq": {
  "enabled": true,
  "sets": [
    { "spl": 0, "points": [ { "freq": 60, "gain": 4, "q": 0.7 } ] },   // reference
    { "spl": 1, "points": [ { "freq": 60, "gain": 8, "q": 0.7 } ] }    // loud (gains differ only)
  ],
  "referenceVolume": 40,   // slider percent 0-100
  "loudVolume": 80,        // slider percent; 0 = no loud anchor
  "loudness": true
}
```

### Migration from v4 and earlier

Applied on boot load *and* on `POST /restore` of an older backup
(`migrate_config` in `ESP/esp-web-server/config.cpp`):

```
referenceVolume = preset.volume
loudVolume      = 0
loudness        = true
```

Anchoring each curve at the level its own preset plays at means the upgrade
reproduces exactly what the old static curve did: no loud anchor to interpolate
toward, and nothing below the reference until the volume is actually turned down.

## HTTP API

All routes take `?preset_name=`, 404 on an unknown preset, and broadcast their
change over `/live-updates` so other open UIs stay in sync.

| route | body | reply |
| --- | --- | --- |
| `PUT /preset/eq/anchors` | `{ referenceVolume, loudVolume }` | 200 `eqAnchorsChanged` |
| `PUT /preset/eq/loud` | `{ gains: [...] }` | 204 (broadcasts `eqLoudChanged`) |
| `DELETE /preset/eq/loud` | — | 200 `eqLoudChanged` |
| `PUT /preset/eq/loudness?enabled=1\|0` | — | 200 `eqLoudnessChanged` |

* **anchors** — percents, 0-100 (400 outside that); an absent key keeps its stored
  value. `loudVolume <= referenceVolume` is stored as `0`, not rejected: it is an
  anchor with no range to interpolate across, which is the same thing as none.
* **loud** — gains align to the reference bands. Longer than the reference band
  count is a 400; shorter leaves the remaining bands flat. The set is created by
  mirroring the reference bands, so it never has to be described.
* **DELETE loud** — removes the set *and* clears `loudVolume`. The reference curve
  is untouched.
* **loudness** — accepts `1`/`0` (and `on`/`off`, like its sibling toggles).

The existing reference routes (`PUT /preset/eq`, `PUT /preset/eq/point`) keep the
loud set mirrored as described under *The two anchors*.

### Websocket messages

```jsonc
{ "messageType": "eqAnchorsChanged",  "presetName": "…", "status": "ok",
  "referenceVolume": 40, "loudVolume": 80 }
{ "messageType": "eqLoudChanged",     "presetName": "…", "status": "ok",
  "loudVolume": 80, "gains": [8, -6, 5] }          // gains: [] = anchor removed
{ "messageType": "eqLoudnessChanged", "presetName": "…", "status": "ok",
  "loudness": true }
```

`eqLoudChanged` carries gains only — the bands always come from the reference
anchor, so a receiver rebuilds the loud set from its own copy of them.

## UART commands

Added to `ESP/esp-web-server/teensy_protocol.h`:

```
setInputEqLoudGain <band> <gain>        # loud-anchor gain for one band, dB
setInputEqAnchors  <refPct> <loudPct>   # 0-100; loud 0 = no loud anchor
setLoudness        <0|1>
```

The Teensy holds the volume law, the interpolation and the shelf pair: the ESP
sends anchors and gains, never a computed curve, so the DSP can re-evaluate on
every volume change without a round trip.

`resetInputEq <fromBand>` zeroes the loud gains from that band too, so trimming
the reference curve needs no extra traffic.

**Ordering.** On preset activation and on every loud write, gains go first and
anchors second: the Teensy only interpolates once it has a loud anchor, so a
half-sent loud curve is never audible. Activation is:

```
setInputEq × n  →  resetInputEq n  →  setInputEqLoudGain × n  →
setInputEqAnchors  →  setLoudness  →  setVolume
```

## Web UI

In the preset editor's EQ section (`WebUI/src/components/shared/EQSection.vue`):

* **Reference volume NN%** with a *Set to current volume* button. Tune by ear at
  a level you like, then anchor the curve there.
* **Loud anchor** — *Add loud anchor at current volume* when there is none
  (disabled, with a hint, until the volume is above the reference), or the
  anchor's percent with *Move to current* and *Remove*. A new loud anchor starts
  as a copy of the reference curve, so placing it changes nothing audibly until
  its gains are edited.
* **Editing: Reference | Loud** — shown only once a loud anchor exists. In *Loud*
  mode the graph edits gains only: frequency and Q inputs are disabled, node
  drags are vertical, REW import and add/delete band are hidden, and a one-line
  hint says why.
* **Loudness compensation below reference** — the on/off switch, with a one-line
  explanation.
* On the graph, a dashed **"Now at NN%"** curve shows what is actually playing —
  the interpolated gains plus the relative loudness compensation — whenever that
  differs from the curve being edited. The active preset's live volume arrives
  over the websocket (`volumeChanged`), so the dashed curve tracks the slider.

The per-output PEQ shares the same `ParametricEQ.vue` component and is untouched:
every dynamic-EQ behaviour is behind the `gainsOnly` / `overlayDb` props, which
default off.
