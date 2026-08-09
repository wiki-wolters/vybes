# WebUI Design Refresh — Implementation Plan

Outcome of a design audit (2026-07-26) of the Vue app in `WebUI/`, run against the mock
server at desktop (1280px) and phone (375px) widths. This plan covers the accepted
findings. All work is frontend-only; no ESP/Teensy/API changes.

## Explicitly deferred (do NOT implement)

- **Editing non-active presets from Home.** The API supports it, but the current
  UX guarantees you always hear what you're editing. Deferred pending product decision.
- **Input-source signal-presence indicators / mixer metaphor.** Needs investigation
  of what per-input data the firmware can stream.

## Verification baseline

- `cd WebUI && npm run test` (vitest unit tests) must pass before and after.
- `npm run test:contract` must pass (starts the mock server itself).
- Visual checks against the mock server (`node mock-server/server.js`, port via `PORT`)
  with `npm run dev` at 375px and 1280px widths.
- No API surface changes: `api-client.js` request/response shapes stay untouched
  (moving/renaming its *call sites* is fine).

---

## Phase 1 — Design tokens & theme unification

The app currently has three palettes: the Tailwind `vybes-*` theme (gray-800/900
surfaces, blue #3b82f6, amber #f59e0b), ParametricEQ's private palette
(#161b22/#1a2029 surfaces, amber #f5c04e, slate #8b96a8, tabular monospace numerals),
and AnalyzerView's hardcoded hexes (#0088ff, #22c55e, #333, #666). The EQ palette is
the best of the three; promote it to the app-wide theme.

1. **Redefine the CSS variables in `src/style.css`** (single source of truth; keep
   `tailwind.config.js` mapped to the same values):
   - Surfaces (from the EQ's richer, cooler ramp):
     - `--vybes-dark` (page bg): `#10141a`
     - `--vybes-dark-element` (card): `#161b22`
     - `--vybes-dark-card` (nested card / raised): `#1a2029`
     - `--vybes-dark-input` (inputs, slider tracks): `#222a35`
     - `--vybes-border`: `rgba(148, 168, 196, 0.16)` — add a solid fallback token
       `#3a4451` where alpha can't be used.
   - Text: `--vybes-text-primary: #e5ebf3`, `--vybes-text-secondary: #8b96a8`
     (bump any place secondary lands on small text below 12px to primary, see Phase 3).
   - Accents: keep blue `#3b82f6`/`#2563eb` as **interactive/primary**; unify amber
     to the EQ's `#f5c04e` (replaces `#f59e0b`/`#fbbf24`) as **live/active state**.
   - Keep the aliases in `tailwind.config.js` (`vybes-blue`, etc.) pointing at the
     new values so existing classes keep working.
2. **Accent semantics — apply consistently:**
   - Blue = interactive: primary buttons, slider thumbs/values, links, selected
     nav item, selected preset, selected tab.
   - Amber = "currently in effect": Active preset badge, enabled-section state,
     the wordmark. Change the preset-editor active tab underline from amber to blue
     (it's a selection, not a live state).
3. **Replace AnalyzerView's hardcoded colors** with tokens/Tailwind classes:
   source trace `#0088ff` → `#3b82f6` (vybes-primary); grid `#333`/labels `#666` →
   the same grid/label colors ParametricEQ uses (`rgba(148,168,196,…)` ramp, `#8b96a8`
   labels). Mic green `#22c55e` can stay (semantic "live" green) but reference it
   via one place.
4. **ParametricEQ.vue**: swap its hardcoded surface hexes to the new shared
   variables (they're the same values now — the point is one source of truth).
   Its amber `#f5c04e` becomes the theme amber automatically.
5. **Font stack**: change to `system-ui, -apple-system, 'Segoe UI', sans-serif` in
   both `style.css` and `tailwind.config.js`. Add `font-variant-numeric: tabular-nums`
   utility usage for all numeric readouts (slider values, chips, FIR pool counts).

## Phase 2 — Layout & use of space

6. **Cap content width on control pages.** Home, Tools, and the preset editor's
   Tuning tab wrap their content in `max-w-3xl mx-auto` (Channels tab and Analyzer
   keep full container width — charts and the channel grid benefit from it).
   Keep the existing edge-to-edge (`px-0`, `rounded-none`) treatment on mobile.
7. **Home becomes two-column at `lg:`**: left column = Presets, Volume, Speakers,
   Mute/Dim; right column = Input Source. (Within the capped width from #6 this
   means `lg:grid lg:grid-cols-2 lg:gap-4` with the cards distributed — use
   `max-w-5xl` for Home instead of 3xl so two columns breathe.)
8. **Move Backup/Restore off Home** into the Tools page (new "Configuration" card
   there — Tools is the utility page and already exists; do NOT add a fourth nav
   tab for two buttons).
9. **Tools grid**: `lg:grid-cols-3` → `sm:grid-cols-2` (there are only 2–3 cards).
10. **Speakers card**: replace the `justify-between` + `w-1/3` flex-wrap with
    `grid grid-cols-2 sm:grid-cols-3 gap-3` so Left/Right/Subwoofer align cleanly.

## Phase 3 — RangeSlider redesign (the highest-leverage component)

11. Restructure `RangeSlider.vue` to a single header row + full-width slider:
    - Row 1: label (left, secondary text) … **value + unit** (right, blue,
      tabular-nums, primary text size).
    - Row 2: the range input, full width. No side-by-side number box.
    - The value readout becomes a **tap/click-to-edit** control: clicking it swaps
      in a small `type="number"` input (`inputmode="decimal"`, auto-focused,
      select-all; Enter/blur commits, Escape cancels). Keep the same
      clamping/rounding behaviour as today (`emitRoundedValue`).
    - Slider thumb: 20px (from 16px) with a `sm:` down-size to 18px on desktop
      pointer devices if desired; keep the focus ring.
    - Accessibility: the number input gets `:aria-label="label"`; the slider keeps
      its `<label for>`.
12. **One value formatter.** Add `formatValue(value, unit, decimals)` to
    `src/utilities.js`: non-breaking space between number and unit ("0 dB",
    "77 %" → use "%" without space, "1000 Hz", kHz collapsing like the EQ chips
    for Hz values ≥ 1000). Use it in RangeSlider's readout, FirPoolBar, and the
    preset editor's "Taps used" line. Sweep the `unit=" Hz"` / `unit="dB"` props
    for consistency (`unit="Hz"`, `unit="dB"`, `unit="%"`).
13. **Q factor display**: 2 decimals (not 3), consistent with the EQ drag readout.

## Phase 4 — Component fixes

14. **ModalDialog.vue** (bug): `position: absolute` → `fixed` (centres in the
    viewport regardless of scroll). Add: Escape-to-close, `role="dialog"`
    `aria-modal="true"` with `:aria-label="title"`, and focus handling (focus the
    dialog on open, return focus on close; a simple Tab wrap within the dialog is
    enough — no dependency).
15. **Mute → Dim.** Rename the Home card title to "Dim", slider label stays
    "Volume reduction", toggle label "Dim". While active, show a persistent amber
    pill ("Dimmed") in the top bar next to the brand (both desktop nav and a
    compact indicator on mobile — reuse the offline-banner slot area, not a new
    banner). Drive it from the existing `/status` fetch + websocket `muteChanged`
    messages if broadcast (check api-client; if no live message exists, App.vue may
    poll status once on mount and HomeView emits through a tiny Pinia store or the
    apiClient's existing connection — pick the simplest working wiring; a shared
    `systemStore` (Pinia) holding mute state that HomeView and App.vue both use is
    acceptable).
16. **Touch targets ≥ 44px** on interactive icons:
    - Preset-edit pencil: enlarge hit area (`p-2.5`, icon `w-5 h-5`) and add
      `aria-label`.
    - `btn-icon` (rename/copy/delete in the editor header): `p-2.5`, icons `w-5 h-5`.
    - CollapsibleSection headers already have large hit areas — fine.
17. **ToggleSwitch a11y**: add an `ariaLabel` prop applied to the checkbox input;
    set it at every call site that passes no visible `label` (section toggles:
    "Enable EQ", "Enable FIR filters", …; ChannelStrip enable: "Enable output N").
18. **Home error banner**: make it dismissable (click, with a small ✕) — same
    pattern as the preset editor's banner.
19. **Number inputs**: add `inputmode="decimal"` in `InputGroup.vue` when
    `type="number"`.
20. **SpeakerDelayInput**: remove the double label ("Delay" heading + "Delay (µs)"
    label + nested card). Render as a plain labelled row like other inputs:
    label = `title` + " (µs)", no nested `bg-vybes-dark-element` card.

## Phase 5 — Channels tab restructure

21. **ChannelStrip becomes a collapsible accordion item.**
    - Collapsed header (always visible): index, label, enable toggle, and a
      one-line summary in secondary text: source · HP · LP · gain · delay ·
      FIR? · PEQ count (e.g. "Left · HP 80 Hz · LP off · 0 dB · 2 PEQ").
      Disabled channels show just index + label + toggle (as today).
    - Expanded: current strip contents. **PEQ stays collapsed-by-default inside**
      (as today via CollapsibleSection).
    - Default state: all collapsed. Expanding one does NOT auto-close others on
      desktop; on mobile (`< sm`) opening a strip closes the previously open one.
    - Keep the store wiring untouched — this is template/presentation only.
22. **Sticky channel switcher on mobile**: a horizontally scrollable chip row
    (`1 Left`, `2 Right`, …, styled like the EQ band chips) that sticks under the
    tab bar (`sticky top-0`) on `< lg`, scrolls to + expands the tapped channel.
23. Grid stays `grid-cols-1 lg:grid-cols-2 2xl:grid-cols-4`; with collapsed-by-default
    strips the height mismatch problem mostly disappears. Use `self-start` /
    `items-start` so expanding one card doesn't stretch its row-mates.

## Phase 6 — Verification & polish pass

24. Run `npm run test` and `npm run test:contract`.
25. Add/update unit tests for `formatValue` in `tests/unit/utilities.test.js`.
26. Manual visual sweep against the mock server at 375px and 1280px:
    Home, editor Tuning + Channels, Analyzer, Tools; modal open on a scrolled
    Channels page (regression check for #14); Dim pill; keyboard: Escape closes
    modal, sliders operable, value-edit commit/cancel.
27. Do not flash the device (`pio ... -t uploadfs`). Building is harmless:
    `ESP/esp-web-server/data/dist` is just a symlink to `WebUI/dist` and
    nothing reaches the device until a flash.

## Out of scope

- Anything under `ESP/`, `Teensy/`, `mock-server/` (except reading).
- `api-client.js` request/response shapes.
- New dependencies (no component libraries, no focus-trap packages).
- The deferred items listed at the top.
