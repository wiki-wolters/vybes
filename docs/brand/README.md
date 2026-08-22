# Vybes brand assets

The wordmark's V is a true inverted Gaussian — a parametric-EQ bell cut
with its control dot floating in a negative-space halo — plus "ybes" set in
Poppins (OFL licence), converted to outlines: Medium in the display cut
(114-unit stems matched to the 110-unit curve), SemiBold in the small cut
(matched to its 145-unit curve). The lockup gap is -15 units — the bell's
tail nests over the "y". No font is needed to use any of these files. All SVGs are filled outlines (no strokes,
no text elements), so they import cleanly into CAD, cutters, and print RIPs.

## Colour

| Token    | Hex       | Use                                            |
|----------|-----------|------------------------------------------------|
| Teal 500 | `#17808D` | Primary. Light backgrounds only (3.7:1 on dark UI — fails AA there). |
| Teal 300 | `#45AEBC` | Wordmark/mark on dark UI surfaces (6.6:1 on `#161b22`). |
| Teal 700 | `#0F5761` | Duotone accents, hover.                        |

CMYK for Teal 500 ≈ C84 M9 Y0 K45 (naive conversion — proof against the
printer's profile before volume runs). Prefer the black mono files for
single-colour print.

## Files

**Screen**
- `vybes-wordmark.svg` — primary (Teal 500, light grounds), ≥ 32 px tall.
- `vybes-wordmark-small.svg` — small-size cut (heavier curve, larger solid
  dot — the halo closes below ~24 px) for 16–32 px tall. Below 16 px use
  the mark alone.
- `vybes-wordmark-reversed.svg` / `-teal-light.svg` / `-black.svg` — white,
  dark-UI teal, and mono colourways of the primary cut.
- `vybes-mark.svg` (+ `-white`, `-black`) — standalone icon-cut mark.
- `vybes-favicon.svg` — 64 px tile, dark ground + Teal 300.
- `vybes-app-icon.svg` — 512 rounded tile (purpose `any`).
- `vybes-app-icon-maskable.svg` — full-bleed, mark inside the 80% safe
  circle (purpose `maskable`). Keep these two as separate manifest entries.
- `vybes-app-icon-dark.svg` — dark-tile alternative.

**Print**
- Use `vybes-wordmark.svg` (coated stock) or `-black.svg` (single colour).
- Minimum height: 8 mm primary cut, 5 mm small cut.
- Clear space all round: the dot diameter (≈ 28% of wordmark height).

**3D printing / CNC / laser**
- `vybes-3d-wordmark-solid.svg` — sized 80 mm wide in the file; thinnest
  feature 2.0 mm and a 1.5 mm halo moat at that size (safe for a 0.4 mm
  nozzle down to ~34 mm wide).
- `vybes-3d-mark-solid.svg` — sized 30 mm; thinnest feature 4.1 mm.
- `vybes-3d-wordmark-stencil.svg` — counters of b/e bridged (the halo dot
  needs no bridge — material connects through the halo openings); use for
  cut-through stencils or deep deboss moulds where islands would fall out.
  Thinnest feature 1.4 mm at 80 mm wide (min width ~46 mm on FDM).
- `vybes-badge.stl` — ready to print: 90×34×2 mm plate, logo raised 1.2 mm.
- `vybes-coin.stl` — ⌀30 mm coin, mark raised 1 mm.
- Emboss (raised) and engrave (recessed) can use the solid files as-is;
  only cut-through work needs the stencil file.

## Regenerating

`tools/` holds the geometry generators (`pip install shapely fonttools
trimesh mapbox_earcut numpy`, then `python gen_final.py` for the SVGs and
`python gen_stl.py` for the STLs, run from `tools/`). The V is generated,
not drawn: `y(x) = tail_y − depth·e^−((x−c)²/2σ²)` — tune `sigma` in
`gen_final.py` to widen or tighten the bell. Poppins Medium and SemiBold TTFs are
included (OFL) so the letterforms regenerate identically.

## Study references (not production assets)

`vybes-wordmark-semibold/-medium/-regular.svg`, `vybes-dot-*.svg` and
`vybes-gap-*.svg` are frozen exploration files backing the study boards on
the design canvas. Don't ship them.
