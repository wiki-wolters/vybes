"""Generate the production Vybes logo set (SVG) + metrics JSON."""
import json
import os
import logolib as L
from shapely.affinity import translate, scale

TEAL = "#17808D"       # Teal 500 — primary, light surfaces
TEAL_LIGHT = "#45AEBC" # Teal 300 — dark surfaces (6.6:1 on #161b22)
TEAL_DEEP = "#0F5761"  # Teal 700 — duotone/hover
DARK = "#10141a"       # UI background

# ---- cuts -----------------------------------------------------------------
# The V is an inverted Gaussian — a parametric-EQ bell cut with its control
# dot. sigma sets the bell width; the tails flatten out naturally.
# Locked 2026-08-22 after the canvas studies: the display cut pairs the
# 110-unit curve with Poppins Medium letters (114-unit stems), floats the dot
# in a 65-unit negative-space halo, and pulls the lockup gap to -15 (the tail
# nests over the "y"). The small cut keeps a solid dot (the halo goes
# hairline below ~24 px) and SemiBold letters (matched to its 145 curve).
PRIMARY = dict(stroke=110, width=1080, tail_y=705, depth=860, sigma=180,
               dot=(0, 120), halo=65)
FONT_DISPLAY = "Poppins-Medium.ttf"
# small-size cut: stem-weight stroke, bigger solid dot, same skeleton
SMALL = dict(PRIMARY, stroke=145, dot=(45, 178), halo=None)
# icon cut: chunkier still, tighter bell, oversized solid dot for tiny squares
ICON = dict(stroke=160, width=1000, tail_y=690, depth=880, sigma=165,
            dot=(45, 195))

GAP = -15
OUT = "out"
os.makedirs(OUT, exist_ok=True)
metrics = {}


def svg_doc(geoms_paths, vb, extra="", title="", desc="", size_attr=""):
    body = "".join(geoms_paths)
    t = f"<title>{title}</title>" if title else ""
    d = f"<desc>{desc}</desc>" if desc else ""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{vb}"{size_attr}>'
            f'{t}{d}{extra}{body}</svg>')


def norm(geom, pad):
    """Translate so bounds start at (pad,pad-ish); return geom, viewbox, flip."""
    minx, miny, maxx, maxy = geom.bounds
    g = translate(geom, xoff=pad - minx, yoff=0)
    flip = maxy + pad
    w = (maxx - minx) + 2 * pad
    h = (maxy - miny) + 2 * pad
    return g, f"0 0 {w:.0f} {h:.0f}", flip, w, h


def emit(name, geom, fill, pad=0, title="", desc="", size_mm=None, bg=None, vb_override=None):
    g, vb, flip, w, h = norm(geom, pad)
    path = L.geom_to_path(g, flip, simplify=0.8)
    size_attr = ""
    if size_mm:
        size_attr = f' width="{size_mm[0]}mm" height="{size_mm[1]}mm"'
    extra = ""
    if bg:
        wf, hf = vb.split()[2:]
        extra = f'<rect width="{wf}" height="{hf}" fill="{bg}"/>'
    doc = svg_doc([f'<path d="{path}" fill="{fill}" fill-rule="evenodd"/>'],
                  vb_override or vb, extra=extra, title=title, desc=desc, size_attr=size_attr)
    with open(f"{OUT}/{name}", "w") as f:
        f.write(doc)
    return dict(file=name, viewBox=vb, w=round(w), h=round(h), path=path)


# ---- wordmarks ------------------------------------------------------------
wm = L.wordmark(PRIMARY, gap=GAP, font_path=FONT_DISPLAY)
wm_small = L.wordmark(SMALL, gap=GAP)

# frozen study reference: the pre-decision lockup (SemiBold, solid dot,
# gap 70) as shown on the Weight study board — never regenerate its look
wm_semibold = L.wordmark(dict(PRIMARY, dot=(50, 150), halo=None), gap=70)
emit("vybes-wordmark-semibold.svg", wm_semibold["geom"], TEAL, pad=40,
    title="Vybes wordmark — SemiBold study reference",
    desc="Frozen pre-decision lockup (SemiBold letters, solid dot, gap 70) for the Weight study board.")

metrics["wordmark"] = emit("vybes-wordmark.svg", wm["geom"], TEAL, pad=40,
    title="Vybes wordmark",
    desc="Primary wordmark: Poppins Medium letters matched to the 110-unit bell, control dot in a negative-space halo, lockup gap -15. Teal 500 #17808D on light backgrounds.")
emit("vybes-wordmark-reversed.svg", wm["geom"], "#FFFFFF", pad=40,
    title="Vybes wordmark reversed", desc="White, for photos/dark grounds.")
emit("vybes-wordmark-teal-light.svg", wm["geom"], TEAL_LIGHT, pad=40,
    title="Vybes wordmark on dark UI", desc="Teal 300 #45AEBC for dark UI surfaces (6.6:1 on #161b22).")
emit("vybes-wordmark-black.svg", wm["geom"], "#000000", pad=40,
    title="Vybes wordmark mono", desc="One-colour black: fax, laser engraving, greyscale print.")
metrics["wordmark_small"] = emit("vybes-wordmark-small.svg", wm_small["geom"], TEAL, pad=40,
    title="Vybes wordmark small-size cut",
    desc="Optical cut for heights under 32 px / 9 mm: stem-weight curve, larger dot.")
emit("vybes-wordmark-small-teal-light.svg", wm_small["geom"], TEAL_LIGHT, pad=40,
    title="Vybes wordmark small cut, dark UI")
emit("vybes-wordmark-small-white.svg", wm_small["geom"], "#FFFFFF", pad=40,
    title="Vybes wordmark small cut, white")

# ---- mark (icon cut) ------------------------------------------------------
mk = L.vmark(ICON)
metrics["mark"] = emit("vybes-mark.svg", mk["geom"], TEAL, pad=40,
    title="Vybes mark", desc="Standalone waveform-V mark, icon cut.")
emit("vybes-mark-white.svg", mk["geom"], "#FFFFFF", pad=40, title="Vybes mark white")
emit("vybes-mark-black.svg", mk["geom"], "#000000", pad=40, title="Vybes mark mono")

# ---- app icons ------------------------------------------------------------
def app_icon(name, tile, mark_fill, scale_frac, rounded, size=512):
    g = mk["geom"]
    minx, miny, maxx, maxy = g.bounds
    gw, gh = maxx - minx, maxy - miny
    s = size * scale_frac / max(gw, gh)
    g2 = scale(g, xfact=s, yfact=s, origin=(0, 0))
    m2 = g2.bounds
    g2 = translate(g2, xoff=(size - (m2[2] - m2[0])) / 2 - m2[0], yoff=0)
    m2 = g2.bounds
    flip = m2[3] + (size - (m2[3] - m2[1])) / 2
    path = L.geom_to_path(g2, flip, simplify=0.6)
    rx = f' rx="{round(size*0.224)}"' if rounded else ""
    doc = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {size} {size}">'
           f'<rect width="{size}" height="{size}"{rx} fill="{tile}"/>'
           f'<path d="{path}" fill="{mark_fill}" fill-rule="evenodd"/></svg>')
    with open(f"{OUT}/{name}", "w") as f:
        f.write(doc)
    return path

app_icon("vybes-app-icon.svg", TEAL, "#FFFFFF", 0.62, rounded=True)
app_icon("vybes-app-icon-maskable.svg", TEAL, "#FFFFFF", 0.52, rounded=False)  # motif inside 80% safe circle
app_icon("vybes-app-icon-dark.svg", DARK, TEAL_LIGHT, 0.62, rounded=True)
app_icon("vybes-favicon.svg", DARK, TEAL_LIGHT, 0.70, rounded=True, size=64)

# ---- 3D print set ---------------------------------------------------------
# Solid single-outline artwork, sized in mm at intended physical size.
# Wordmark at 80 mm wide (the production display cut); mark at 30 mm wide.
wm3d = wm["geom"]
b = wm3d.bounds
w_units = b[2] - b[0]
mmw = 80.0
f_units = L.min_feature(wm3d)
metrics["3d_wordmark"] = {
    "min_feature_units": round(f_units, 1),
    "min_feature_mm_at_80mm": round(f_units * mmw / w_units, 2),
    "halo_gap_mm_at_80mm": round(65 * mmw / w_units, 2),
}
h_mm = (b[3] - b[1]) * mmw / w_units
emit("vybes-3d-wordmark-solid.svg", wm3d, "#000000", pad=0,
     title="Vybes wordmark — 3D solid",
     desc=f"Single-outline artwork for extrusion. At 80 mm wide the thinnest feature is "
          f"{metrics['3d_wordmark']['min_feature_mm_at_80mm']} mm.",
     size_mm=(round(mmw, 2), round(h_mm, 2)))

mk3d = mk["geom"]
bm = mk3d.bounds
mw_units = bm[2] - bm[0]
mmw_mark = 30.0
f_units_m = L.min_feature(mk3d)
metrics["3d_mark"] = {
    "min_feature_units": round(f_units_m, 1),
    "min_feature_mm_at_30mm": round(f_units_m * mmw_mark / mw_units, 2),
}
mh_mm = (bm[3] - bm[1]) * mmw_mark / mw_units
emit("vybes-3d-mark-solid.svg", mk3d, "#000000", pad=0,
     title="Vybes mark — 3D solid",
     desc=f"Single-outline mark for extrusion. At 30 mm wide the thinnest feature is "
          f"{metrics['3d_mark']['min_feature_mm_at_30mm']} mm.",
     size_mm=(round(mmw_mark, 2), round(mh_mm, 2)))

# ---- stencil (bridged counters) -------------------------------------------
# Bridge the enclosed counters of b (bowl -> right) and e (eye -> up) so a
# cut-through stencil keeps no floating islands.
sten = wm3d
letter_geoms = wm["letters"]  # y, b, e, s
BRIDGE_W = 95
for idx, ch, direction in [(1, "b", "right"), (2, "e", "up")]:
    lg = letter_geoms[idx]
    polys = [lg] if lg.geom_type == "Polygon" else list(lg.geoms)
    for p in polys:
        for hole in p.interiors:
            hx = sum(c[0] for c in hole.coords) / len(hole.coords)
            hy = sum(c[1] for c in hole.coords) / len(hole.coords)
            sten = L.stencil_bridge(sten, (hx, hy), direction, BRIDGE_W)
f_units_s = L.min_feature(sten)
metrics["3d_stencil"] = {"min_feature_mm_at_80mm": round(f_units_s * mmw / w_units, 2)}
emit("vybes-3d-wordmark-stencil.svg", sten, "#000000", pad=0,
     title="Vybes wordmark — stencil-safe",
     desc="Counters of b and e bridged: safe for cut-through stencils and deep deboss moulds.",
     size_mm=(round(mmw, 2), round(h_mm, 2)))

# ---- metrics + inline paths for artboards ---------------------------------
metrics["colors"] = {"teal500": TEAL, "teal300": TEAL_LIGHT, "teal700": TEAL_DEEP}
mm = wm["geom"].bounds
metrics["wordmark_bounds"] = [round(v) for v in mm]
with open(f"{OUT}/metrics.json", "w") as f:
    json.dump(metrics, f, indent=1)
print(json.dumps({k: v for k, v in metrics.items() if "path" not in str(type(v)) and k.startswith("3d")}, indent=1))
print("files:", sorted(os.listdir(OUT)))
