# Auto-FIR Correction (Design)

Status: **draft for discussion**, 2026-08-30. Nothing here is built. This
captures the design conversation so far; the open questions at the bottom need
decisions before implementation starts.

## Goal

Generate FIR correction filters in the browser from the device's own
measurements, replacing the manual rePhase workflow. Today the FIR path assumes
the user designs filters in external tools; the most powerful feature of the
device is gated on knowing rePhase/REW. The measurement plumbing to close that
loop already exists: chirp playback with a Teensy-side sequencer (delay probe),
per-output soloing (`soloOutput` keepalive), phone-mic capture with calibration
profiles, and a verbatim rePhase-WAV loader.

This is time-domain (phase) correction plus fine magnitude trim — the thing
MiniDSP sells as Dirac. It is *not* a replacement for the parametric auto-EQ,
which keeps a distinct job (see next section).

## Division of labor

Biquads are minimum-phase: their phase is locked to their magnitude. Only FIR
can correct **excess phase** — crossover rotation (an LR4 acoustic crossover
rotates 360°) and non-delay driver offsets — because phase *lead* relative to
minimum phase must be bought with bulk latency. That is the part of the
correction problem currently outsourced to rePhase.

Below the room's modal transition (~300–500 Hz) the argument flips: FIR
resolution is linear in frequency (4,096 taps ≈ 93 ms ≈ 12 Hz resolution —
marginal for a 35 Hz mode a single biquad notches for free), phase correction
there costs tens of ms of latency, and deep modal nulls are position-dependent
and dangerous to invert. So the layers are:

| Band | Correction | Tool | Measurement |
|---|---|---|---|
| Below transition | Room magnitude | Shared input EQ biquads (existing auto-EQ) | Multi-position pink noise (existing) |
| Above transition | Driver linearization, crossover phase, magnitude trim | Per-output FIR (this design) | Stationary-mic sweeps, time-gated |
| Sub band | Level/delay/polarity/PEQ | Existing tools; **no FIR** | Delay probe + auto-EQ (existing) |

This split is also what makes the tap pool sufficient: the expensive
low-frequency correction never gets duplicated into eight kernels.

Run order for the user: FIR wizard first, then the room auto-EQ against the
corrected speakers.

## Measurement

### Sweeps, not pink noise

The pink-noise RTA path gives a magnitude spectrum only — phone mic and Teensy
share no clock and no reference channel, so a stochastic signal can never yield
phase, and without phase there is no impulse response to design against. A
deterministic excitation with known timing does: an exponential (Farina/ESS)
sweep, deconvolved with its inverse filter. The delay probe already is most of
this — cross-correlating a known chirp *is* deconvolution; today we keep one
number from the correlation trace (the peak) and discard the rest. This design
keeps the whole deconvolved impulse response.

**Standing decision to revisit**: "no sweep-based EQ measurement" (2026-08)
was about *handheld/moving-mic* capture, where movement breaks sweep
deconvolution. This design uses a **stationary** mic — a different regime — but
the decision should be revisited explicitly, not silently overridden.

Side effect for later: ESS deconvolution separates harmonic distortion orders
into distinct pre-arrivals in the IR, so a THD-vs-frequency analyzer falls out
of the same capture nearly for free. Out of scope here.

### One position, time-gated — why reflections at the seat don't poison it

The obvious objection: reflections cause peaks at the mic position that don't
exist a few cm away. The defense is not averaging but **time-gating**. A
position-sensitive comb peak is the direct sound plus a copy arriving ms later;
in the IR they are different samples. Gate the IR to the direct arrival before
designing, and the reflections never enter the data — they arrived after the
window closed (standard quasi-anechoic technique). The gated direct field
varies only slowly with position.

The gate sets a resolution floor (~250 Hz for a 4 ms gate ahead of a typical
floor bounce) — which coincides with the transition where correction is handed
to the biquad flow anyway. Concretely:

- Frequency-dependent windowing (a constant few cycles): fine resolution up
  high, degrading gracefully downward.
- Fractional-octave smoothing on top: never invert ripple narrower than
  ~1/6 octave even in gated data.
- Excess phase is the most position-robust quantity of all — crossover phase
  rotation is a property of the drivers and filters, not the mic spot.
- Delays and polarity are the one place position-specificity is *desired*:
  time-of-flight to the actual seat.

Optional confidence step for the mid-band (200–800 Hz, where gates get long):
"add a position" — re-run the sweep sequence from a second/third **stationary**
spot, average **magnitudes only**, keep phase from the primary seat (complex
averaging across positions smears phase through differing path lengths).

### Capture hygiene

- Two sweeps per output, back to back: adaptive mic processing (AGC, noise
  suppression, Voice Isolation) shows up as inconsistency between them —
  **refuse to fit** on mismatch rather than encoding the mic's behavior into
  the correction. (Field incident: an iPhone 17 Pro auto-correction hollowed
  the vocal band; prime suspect is iOS Mic Mode / Voice Isolation, which sits
  below getUserMedia and is invisible to constraint readback. The wizard should
  tell iOS users to set Wide Spectrum.)
- Clock drift: phone ADC and Teensy DAC are on independent crystals; 50 ppm
  smears the deconvolved IR at high frequencies over a multi-second sweep.
  Estimate drift from the spacing error between the two sweeps (the sequencer
  already supports spaced chirps) and resample the capture before deconvolving.
- Mic coloration is common-mode across every capture from one position, so all
  *relative* quantities — L/R matching, crossover blending, delays, excess
  phase — are mic-agnostic. Absolute magnitude targets are the exposed part:
  correct assertively only below the transition (delegated to the biquad flow)
  and cap correction in the presence band (±3 dB in 1–5 kHz) above it.
- Existing FIRs are bypassed during measurement (the preset-level `firEnabled`
  flag) so old correction doesn't compound into new. Crossovers and PEQ stay
  active: the acoustic crossover is the thing being phase-linearized, and the
  design should compose with the EQ, not fight it.

## Filter design

### Latency budget and the two renders

The N/2 group delay belongs to linear-phase kernels specifically. A
minimum-phase kernel of the same length has essentially no inherent delay —
processing latency collapses to the first-partition size (128 samples ≈ 2.9 ms
at 44.1 kHz). The lookahead needed for phase correction scales ~1/f: under a
ms linearizes a 2.5 kHz crossover, a few ms reaches 300 Hz, tens of ms for a
sub crossover. So latency is a knob, not a binary:

- Every kernel gets the **identical bulk lead** (the chosen budget), so
  inter-output time alignment survives correction and the delay fields stay
  meaningful for trim.
- One measurement renders **twice**: a *gaming* render (0 ms budget → pure
  minimum-phase, ~3 ms latency, magnitude-only correction) and a *music* render
  (e.g. 10 ms budget → linearizes crossovers down to roughly one period of the
  budget, ~13 ms total — inside the 20 ms envelope).
- Frequency-dependent windowing in the designer confines phase correction to
  where the budget can reach; below that, minimum-phase only.

### Tap budget

The pool is 12,288 taps, charged in 128-tap partitions, shared across all
outputs. Allocation is derived from the preset topology, not hardcoded: an
output's kernel needs time-support reaching only its own passband's low edge,
so **taps ∝ 1/f_lo**, quantized to partitions. Subs get zero (see division of
labor). Worked example — 3-way stereo + 2 subs at 44.1 kHz:

| Output | Passband | Taps each | Window |
|---|---|---|---|
| Tweeters ×2 | 2.5k+ | 512 | ~11 ms |
| Mids ×2 | 300–2.5k | 1,536 | ~35 ms |
| Woofers ×2 | 80–300 | 3,584 | ~81 ms (~12 Hz resolution) |
| Subs ×2 | <80 | 0 | — |

Total 11,264 of 12,288. **Do not plan to exact-fit**: full-pool loads work but
took three crash fixes to get there; the planner defaults to leaving at least a
partition or two free. CPU note: the MAC loop dominates convolution cost and
cache stalls grow past ~16 partitions per filter, so total tap count is the
budget that matters, and it's unchanged from today's worst case.

## User workflow

0. **Plan** — wizard reads the active preset's topology (enabled outputs,
   hp/lp crossover assignments) and shows the proposed per-output tap budget
   against the FIR pool bar *before any sound plays*. The pool is the
   constraint, so it's the first screen, not an apply-time error. User can
   adjust; defaults should be right for any topology.
1. **Setup** — stationary mic at the seat (explicitly framed as different from
   the moving-mic pink-noise flow). Capture-chain readback check; Wide
   Spectrum guidance on iOS. Preset `firEnabled` off for the session.
2. **Measure** — one continuous capture session; sequencer solos each output
   in turn (existing keepalive + amp ramp), two ESS sweeps per output
   (consistency check, drift estimate, average). Shared timebase → relative
   delays between all outputs fall out for free; this replaces a separate
   delay-probe pass.
3. **Review** — per-output magnitude and excess phase with automated flags:
   polarity inversions, low-SNR captures, and outputs whose measured passband
   contradicts their hp/lp config (generalizing the 2026-08-27 sub trap).
4. **Design** — two global choices: per-way targets shared L/R (matching
   between sides beats absolute accuracy above the transition), and the
   latency budget. Render gaming + music kernels; show predicted latency for
   each.
5. **Apply** — copy the preset first (undo and A/B in one move). Write kernels
   as rePhase-compatible WAVs named per output + version, upload to SD, assign
   to outputs, write delays from the same measurement.
6. **Verify** — one short re-sweep pass, before/after overlay.

## Firmware prerequisites (gaps as of 2026-08-30)

1. **FIR file upload path** — there is none. `api_fir.cpp` has list / enable /
   pool / assign routes only. Needs chunked browser → ESP → UART → Teensy → SD
   transfer, with the ESP's heap fragility in mind (small chunks, no large
   buffered bodies).
2. **Full IR return to the browser** — the delay probe returns a delay number;
   this needs the deconvolved capture (or the raw capture for browser-side
   deconvolution — likely the better split: browser has the FFT muscle and the
   captured audio already lives there; the Teensy only needs to sequence and
   solo). Amount of new Teensy work may therefore be small.
3. **Sweep excitation** — confirm the probe chirp is (or becomes) a true
   exponential sweep with a defined inverse filter, at a level/duration suited
   to measurement rather than just delay detection.

## Out of scope

- Multi-sub optimization (MSO-style seat-to-seat smoothing across subs) — real
  feature, separate design.
- THD analyzer from the same captures — falls out later, not part of this.
- Multirate sub FIR (decimate /8) — already deferred in CHANNEL_ARCHITECTURE;
  unneeded here since subs get no taps.

## Open questions

- Revisit the "no sweeps" decision for the stationary case (see Measurement).
- Where does deconvolution run — browser (preferred above) or device?
- Upload protocol framing over the UART link; file naming/versioning scheme
  and SD cleanup for superseded auto-generated filters.
- Default transition frequency between FIR and biquad territory (fixed
  ~300–500 Hz vs derived from the gate the room actually allows).
- Whether the music render's default budget (10 ms) should be user-visible as
  a number or as a "max phase correction depth" frequency.
