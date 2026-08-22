"""Vybes logo geometry generator.

Builds the waveform-V + 'ybes' wordmark as real filled outlines
(shapely polygons) so the output SVGs are print- and CAD-ready.
Font coords are y-up (baseline = 0); SVG emit flips y.
"""
from fontTools.ttLib import TTFont
from fontTools.pens.recordingPen import RecordingPen
from shapely.geometry import Polygon, Point, LineString, box
import math
from shapely.ops import unary_union
import math

FONT_PATH = "Poppins-SemiBold.ttf"
_font_cache = {}


def _font(path=FONT_PATH):
    if path not in _font_cache:
        _font_cache[path] = TTFont(path)
    return _font_cache[path]


def _sample_quad(p0, p1, p2, steps):
    pts = []
    for i in range(1, steps + 1):
        t = i / steps
        mt = 1 - t
        x = mt * mt * p0[0] + 2 * mt * t * p1[0] + t * t * p2[0]
        y = mt * mt * p0[1] + 2 * mt * t * p1[1] + t * t * p2[1]
        pts.append((x, y))
    return pts


def sample_cubic(p0, p1, p2, p3, steps):
    pts = []
    for i in range(1, steps + 1):
        t = i / steps
        mt = 1 - t
        x = (mt**3) * p0[0] + 3 * (mt**2) * t * p1[0] + 3 * mt * (t**2) * p2[0] + (t**3) * p3[0]
        y = (mt**3) * p0[1] + 3 * (mt**2) * t * p1[1] + 3 * mt * (t**2) * p2[1] + (t**3) * p3[1]
        pts.append((x, y))
    return pts


def glyph_geom(ch, steps=16, path=FONT_PATH):
    """Return (shapely geometry, advance width) for a character, font units."""
    f = _font(path)
    gs = f.getGlyphSet()
    gname = f.getBestCmap()[ord(ch)]
    pen = RecordingPen()
    gs[gname].draw(pen)
    contours, cur, last = [], [], None
    for op, pts in pen.value:
        if op == "moveTo":
            cur = [pts[0]]
            last = pts[0]
        elif op == "lineTo":
            cur.append(pts[0])
            last = pts[0]
        elif op == "qCurveTo":
            points = list(pts)
            if points[-1] is None:  # all-off-curve contour
                points[-1] = ((points[0][0] + cur[0][0]) / 2, (points[0][1] + cur[0][1]) / 2)
            offs, final = points[:-1], points[-1]
            p0 = last
            for i, off in enumerate(offs):
                if i < len(offs) - 1:
                    nxt = ((off[0] + offs[i + 1][0]) / 2, (off[1] + offs[i + 1][1]) / 2)
                else:
                    nxt = final
                cur.extend(_sample_quad(p0, off, nxt, steps))
                p0 = nxt
            last = final
        elif op == "curveTo":
            cur.extend(sample_cubic(last, pts[0], pts[1], pts[2], steps))
            last = pts[2]
        elif op == "closePath":
            if len(cur) >= 3:
                contours.append(cur)
            cur = []
    geom = None
    for c in contours:
        p = Polygon(c)
        if not p.is_valid:
            p = p.buffer(0)
        geom = p if geom is None else geom.symmetric_difference(p)
    adv = f["hmtx"][gname][0]
    return geom, adv


def vmark(params):
    """Waveform-V: a true inverted Gaussian (parametric-EQ bell cut) with its
    control dot. Centerline y(x) = tail_y - depth * exp(-(x-c)^2 / (2 sigma^2)),
    sampled and buffered to a filled outline. Font units, y-up.
    """
    st = params["stroke"]
    w = params["width"]
    tail_y = params["tail_y"]
    depth = params["depth"]
    sigma = params["sigma"]
    cx = params.get("center", w / 2)
    n = 160
    c = []
    for i in range(n + 1):
        x = w * i / n
        y = tail_y - depth * math.exp(-((x - cx) ** 2) / (2 * sigma * sigma))
        c.append((x, y))
    line = LineString(c)
    stroke_geom = line.buffer(st / 2, quad_segs=24)
    dot_dy, dr = params["dot"]  # dot center offset below curve minimum, radius
    dcx, dcy = cx, (tail_y - depth) - dot_dy
    dot = Point(dcx, dcy).buffer(dr, quad_segs=48)
    halo = params.get("halo")  # gap width: punch the curve out around the dot
    if halo:
        stroke_geom = stroke_geom.difference(
            Point(dcx, dcy).buffer(dr + halo, quad_segs=48))
    return {
        "geom": unary_union([stroke_geom, dot]),
        "centerline": c,
        "dot": (dcx, dcy, dr),
        "advance": w,
    }


def wordmark(vparams, tracking=0, pair_adjust=None, letters="ybes", gap=None, font_path=FONT_PATH):
    """Compose V-mark + letters. gap = space between V right stub end and 'y'."""
    v = vmark(vparams)
    geoms = [v["geom"]]
    letter_geoms = []
    x = v["advance"] + (gap if gap is not None else 120)
    pair_adjust = pair_adjust or {}
    for ch in letters:
        g, adv = glyph_geom(ch, path=font_path)
        g = shapely_translate(g, x, 0)
        letter_geoms.append(g)
        x += adv + tracking + pair_adjust.get(ch, 0)
    return {
        "v": v,
        "letters": letter_geoms,
        "geom": unary_union(geoms + letter_geoms),
        "width": x,
    }


def shapely_translate(g, dx, dy):
    from shapely.affinity import translate
    return translate(g, xoff=dx, yoff=dy)


def shapely_scale(g, s, origin=(0, 0)):
    from shapely.affinity import scale
    return scale(g, xfact=s, yfact=s, origin=origin)


def _ring_to_path(coords, flip_h, nd=1):
    parts = []
    fmt = f"%.{nd}f"
    for i, (x, y) in enumerate(coords):
        cmd = "M" if i == 0 else "L"
        parts.append(f"{cmd}{fmt % x} {fmt % (flip_h - y)}")
    parts.append("Z")
    return "".join(parts)


def geom_to_path(geom, flip_h, nd=1, simplify=1.0):
    """Shapely geometry -> SVG path string (evenodd), y flipped about flip_h."""
    if simplify:
        geom = geom.simplify(simplify)
    polys = [geom] if geom.geom_type == "Polygon" else list(geom.geoms)
    d = []
    for p in polys:
        d.append(_ring_to_path(list(p.exterior.coords), flip_h, nd))
        for hole in p.interiors:
            d.append(_ring_to_path(list(hole.coords), flip_h, nd))
    return "".join(d)


def stencil_bridge(geom, hole_pt, direction, width, reach=2000):
    """Subtract a strip from geom so the counter at hole_pt connects out.
    direction: 'up'|'down'|'left'|'right' (y-up coords)."""
    x, y = hole_pt
    w2 = width / 2
    if direction == "up":
        strip = box(x - w2, y, x + w2, y + reach)
    elif direction == "down":
        strip = box(x - w2, y - reach, x + w2, y)
    elif direction == "right":
        strip = box(x, y - w2, x + reach, y + w2)
    else:
        strip = box(x - reach, y - w2, x, y + w2)
    return geom.difference(strip)


def min_feature(geom, lo=1.0, hi=250.0, tol=0.5, keep=0.985):
    """Thinnest feature via morphological opening: 2*max radius r such that
    erode-then-dilate still keeps `keep` of the area (features thinner than
    2r are destroyed by the opening and drop the area)."""
    area = geom.area
    while hi - lo > tol:
        mid = (lo + hi) / 2
        opened = geom.buffer(-mid).buffer(mid)
        if opened.area / area < keep:
            hi = mid
        else:
            lo = mid
    return 2 * lo
