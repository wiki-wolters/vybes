"""Printable STL badge: rounded plate + raised Vybes wordmark (small cut)."""
import numpy as np
import trimesh
from shapely.geometry import box
from shapely.affinity import translate, scale
import logolib as L
from gen_final import PRIMARY, ICON, GAP, FONT_DISPLAY

# Badge: 90 x 34 x 2 mm plate, logo raised 1.2 mm on top.
# Uses the production display cut (Medium letters, halo dot): at 78 mm wide
# the halo gap is ~1.4 mm — well clear of a 0.4 mm nozzle.
PLATE_W, PLATE_H, PLATE_T = 90.0, 34.0, 2.0
LOGO_RAISE = 1.2
LOGO_W = 78.0  # logo width on the plate

wm = L.wordmark(PRIMARY, gap=GAP, font_path=FONT_DISPLAY)["geom"]
b = wm.bounds
s = LOGO_W / (b[2] - b[0])
logo = scale(wm, xfact=s, yfact=s, origin=(0, 0))
lb = logo.bounds
logo = translate(logo,
                 xoff=(PLATE_W - (lb[2] - lb[0])) / 2 - lb[0],
                 yoff=(PLATE_H - (lb[3] - lb[1])) / 2 - lb[1])

plate = box(0, 0, PLATE_W, PLATE_H).buffer(-0.0)
plate = box(0, 0, PLATE_W, PLATE_H).buffer(4).buffer(-8).buffer(4)  # rounded corners r=4

plate_mesh = trimesh.creation.extrude_polygon(plate, height=PLATE_T)
logo_polys = [logo] if logo.geom_type == "Polygon" else list(logo.geoms)
logo_meshes = []
for p in logo_polys:
    m = trimesh.creation.extrude_polygon(p, height=LOGO_RAISE)
    m.apply_translation([0, 0, PLATE_T])
    logo_meshes.append(m)

badge = trimesh.util.concatenate([plate_mesh] + logo_meshes)
badge.export("out/vybes-badge.stl")
print("badge:", badge.bounds.round(2).tolist(), "watertight parts:",
      all(m.is_watertight for m in [plate_mesh] + logo_meshes))

# Mark-only emboss coin: 30 mm round coin, raised icon-cut mark.
COIN_D, COIN_T = 30.0, 2.4
from shapely.geometry import Point
coin = Point(COIN_D / 2, COIN_D / 2).buffer(COIN_D / 2, quad_segs=64)
mk = L.vmark(ICON)["geom"]
mb = mk.bounds
ms = (COIN_D * 0.62) / max(mb[2] - mb[0], mb[3] - mb[1])
mk2 = scale(mk, xfact=ms, yfact=ms, origin=(0, 0))
mb2 = mk2.bounds
mk2 = translate(mk2,
                xoff=(COIN_D - (mb2[2] - mb2[0])) / 2 - mb2[0],
                yoff=(COIN_D - (mb2[3] - mb2[1])) / 2 - mb2[1])
coin_mesh = trimesh.creation.extrude_polygon(coin, height=COIN_T)
mark_mesh = trimesh.creation.extrude_polygon(mk2, height=1.0)
mark_mesh.apply_translation([0, 0, COIN_T])
coin_out = trimesh.util.concatenate([coin_mesh, mark_mesh])
coin_out.export("out/vybes-coin.stl")
print("coin:", coin_out.bounds.round(2).tolist())
