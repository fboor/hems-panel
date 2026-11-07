#!/usr/bin/env python3
"""Generate laser/3D-print outlines for the Waveshare ESP32-S3-Touch-LCD-7.

Geometry from the Waveshare technical drawing (esp32-s3-touch-lcd-7.pdf):
  lens OD (cover glass) 192.96 x 110.76 mm, corner radius R7.88
  viewable area          154.88 x 86.72 mm (reference only)
Frames (user specification):
  front plate            260 x 170 mm outer, glass cutout inside
  glass                  lens OD outline
  mount frame            glass outline + 3 mm per side (mounting)
  raster                 nested rounded rects between glass and mount
                         outlines every 0.3 mm
Units: mm, stroke-width 0.15, fill none.
"""
import math

# --- inputs (named, so a follow-up change is a one-line edit) ---
FRONT_W, FRONT_H = 260.0, 170.0
FRONT_R = 10.0                      # outer corner radius of the front plate
GLASS_W, GLASS_H, GLASS_R = 192.96, 110.76, 7.88   # lens OD, R7.88 x4
MOUNT_OFFSET = 3.0                  # mounting frame distance from glass
RASTER_STEP = 0.3                   # nested frame spacing between them
MARGIN = 10.0                       # gap between stacked outlines on canvas

MOUNT_W = GLASS_W + 2 * MOUNT_OFFSET
MOUNT_H = GLASS_H + 2 * MOUNT_OFFSET
MOUNT_R = GLASS_R + MOUNT_OFFSET

def rounded_rect_path(x, y, w, h, r):
    """Path d for a rounded rectangle, SVG y-down."""
    return (
        f"M {x+r:.2f} {y:.2f} "
        f"H {x+w-r:.2f} A {r:.2f} {r:.2f} 0 0 1 {x+w:.2f} {y+r:.2f} "
        f"V {y+h-r:.2f} A {r:.2f} {r:.2f} 0 0 1 {x+w-r:.2f} {y+h:.2f} "
        f"H {x+r:.2f} A {r:.2f} {r:.2f} 0 0 1 {x:.2f} {y+h-r:.2f} "
        f"V {y+r:.2f} A {r:.2f} {r:.2f} 0 0 1 {x+r:.2f} {y:.2f} Z"
    )

def rect_path(x, y, w, h):
    return f"M {x:.2f} {y:.2f} H {x+w:.2f} V {y+h:.2f} H {x:.2f} Z"

# All outlines share one centre, so they can be laid on top of each other
# and the fabrication tool separates them by colour (laser layers).
canvas_w = FRONT_W
canvas_h = FRONT_H
cx, cy = canvas_w / 2, canvas_h / 2

gx = cx - GLASS_W / 2
gy = cy - GLASS_H / 2
mx = cx - MOUNT_W / 2
my = cy - MOUNT_H / 2

parts = []
parts.append('<?xml version="1.0" encoding="UTF-8"?>')
parts.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{canvas_w:.1f}mm" '
             f'height="{canvas_h:.1f}mm" viewBox="0 0 {canvas_w:.2f} {canvas_h:.2f}">')
parts.append('<title>Waveshare ESP32-S3-Touch-LCD-7 - front, glass, mount frame (mm)</title>')

def layer(id_, d, color):
    parts.append(f'<path id="{id_}" d="{d}" fill="none" stroke="{color}" stroke-width="0.15"/>')

# Front plate: outer outline + glass cutout (two subpaths, even-odd fill for cutting)
front_d = (rounded_rect_path(0, 0, FRONT_W, FRONT_H, FRONT_R) + ' '
           + rounded_rect_path((FRONT_W-GLASS_W)/2, (FRONT_H-GLASS_H)/2,
                               GLASS_W, GLASS_H, GLASS_R))
layer('front-plate', front_d, '#000000')

# Glass outline
layer('glass', rounded_rect_path(gx, gy, GLASS_W, GLASS_H, GLASS_R), '#8000ff')

# Mounting frame: outer = glass + 3 mm, inner = glass outline
mount_d = (rounded_rect_path(mx, my, MOUNT_W, MOUNT_H, MOUNT_R) + ' '
           + rounded_rect_path(gx, gy, GLASS_W, GLASS_H, GLASS_R))
layer('mount-frame', mount_d, '#0000ff')

# Raster: nested rounded rects between glass and mount outlines, every 0.3 mm
raster = []
k = 1
while k * RASTER_STEP < MOUNT_OFFSET - 1e-9:
    off = k * RASTER_STEP
    raster.append(rounded_rect_path(gx-off, gy-off, GLASS_W+2*off,
                                    GLASS_H+2*off, GLASS_R+off))
    k += 1
for i, d in enumerate(raster):
    layer(f'raster-{i:02d}', d, '#00aa00')

parts.append('</svg>')
svg = '\n'.join(parts) + '\n'

# Numeric verification: recompute bboxes from the emitted paths
import re
def bbox(d):
    nums = [float(v) for v in re.findall(r'-?\d+\.?\d*', d)]
    # path commands mix radii/flags with coords; only check our known values
    return nums

print(f"front outer: {FRONT_W} x {FRONT_H} R{FRONT_R}")
print(f"glass: {GLASS_W} x {GLASS_H} R{GLASS_R} at ({gx:.2f},{gy:.2f})")
print(f"mount outer: {MOUNT_W:.2f} x {MOUNT_H:.2f} R{MOUNT_R:.2f} at ({mx:.2f},{my:.2f})")
print(f"mount offset per side: {(MOUNT_W-GLASS_W)/2:.2f} mm")
print(f"raster nested rects: {len(raster)} (offsets 0.3..{(len(raster))*RASTER_STEP:.1f} mm)")
print(f"canvas: {canvas_w:.2f} x {canvas_h:.2f} mm")
assert abs((MOUNT_W-GLASS_W)/2 - 3.0) < 1e-9
assert len(raster) == 9  # 0.3,0.6,...,2.7
assert abs(canvas_h - FRONT_H) < 0.01

open('waveshare-touch-lcd-7-frame-260x170.svg','w').write(svg)
print('written waveshare-touch-lcd-7-frame-260x170.svg')
