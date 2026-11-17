# Blender (blender -b -P tools/make_xe_keyboard.py, from the repository root):
# the 130XE keyboard as a mesh model, tools/keyboards/atari-xe-MX.blend, the
# same conventions as c64-breadbin-SA.blend:
#   Caps     the keycaps: the A130KB_MX keycap (tools/keyboards/keycap-1u.stl)
#            stretched to each key of the layout, the 9u SA spacebar of the
#            C64 keyboard
#   Legends  the legends as geometry: the opaque pixels of the legend texture
#            (xe-keys.png of tools/make_screens.py) on the tops of the caps (0.15 mm over them)
# Metres; X to the right of who types, Y from the rear edge of the top row
# (0) to the front (negative), Z up from the base of the caps (0); the left
# edge of key (0, 0) at x 0. The function keys are not part of it.
# Environment: DESK_SCREENS (folder of tools/make_screens.py), XE_KB_OUT
# (default tools/keyboards/atari-xe-MX.blend), XE_KB_STEP (texture pixels
# per legend quad, default 1).
import json
import os
import sys

import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ROOT = os.getcwd()
sys.path.insert(0, os.path.join(ROOT, "tools"))
import desk_scene as ds  # noqa: E402

U = ds.U
env = os.environ.get
SCREENS = env("DESK_SCREENS", "screens")
OUT = os.path.abspath(env("XE_KB_OUT") or os.path.join(ROOT, "tools", "keyboards", "atari-xe-MX.blend"))
STEP = int(env("XE_KB_STEP", "1"))
KB = os.path.join(ROOT, "tools", "keyboards")

for o in list(bpy.data.objects):
    bpy.data.objects.remove(o)

# the keycap (Y up in its file -> Z up; its +Z is the front: -Y here)
cv, cf = ds.read_stl(os.path.join(KB, "keycap-1u.stl"))
cap = [(-x, z, y) for x, y, z in cv]   # turned 180 degrees as the XE frame
sv, sf = ds.sa_spacebar(KB)                    # +X left, +Y front: turn back
sbar = [(-x, -y, z) for x, y, z in sv]
lay = json.load(open(os.path.join(SCREENS, "xe-keys.json")))["keys"]
cols = max(k[0] + k[2] for k in lay)
rows = max(k[1] + k[3] for k in lay)

verts, faces, uvs = [], [], []
for kx, ky, kw, kh, _ in lay:
    cx = (kx + kw / 2) * U
    cy = -(ky + kh / 2) * U
    shape, fshape = cap, cf
    sx = (kw * U - 0.85) / 18.2
    sy = (kh * U - 0.85) / 18.2
    if kw >= 6:
        shape, fshape, sx, sy = sbar, sf, 1.0, 1.0
    base = len(verts)
    for x, y, z in shape:
        px, py = cx + x * sx, cy + y * sy
        uvs.append((px / U / cols, 1 + py / U / rows))
        verts.append((px, py, z))
    faces += [[base + i for i in f] for f in fshape]

me = bpy.data.meshes.new("Caps")
me.from_pydata(verts, [], faces)
uv = me.uv_layers.new(name="legends")
for poly in me.polygons:
    for li in poly.loop_indices:
        uv.data[li].uv = uvs[me.loops[li].vertex_index]
me.update()
caps = bpy.data.objects.new("Caps", me)
bpy.context.scene.collection.objects.link(caps)
caps.data.materials.append(ds.plastic("xe_keys", ds.XE["key"], 0.4))

# the legends: texture pixel (u, v) -> layout (u * cols, (1 - v) * rows) keys
# -> x, y; z on the top of the cap below (a ray from above), 0.15 mm over it
tree = BVHTree.FromPolygons([Vector(v) for v in verts], faces)
img = bpy.data.images.load(os.path.join(SCREENS, "xe-keys.png"))
W, H = img.size
px = img.pixels[:]


def top(x, y):
    hit = tree.ray_cast(Vector((x, y, 100.0)), Vector((0, 0, -1)))
    # (the keycap file's winding is not consistent: either side up)
    if hit[0] is None or abs(hit[1].z) < 0.6:
        return None
    return hit[0] + Vector((0, 0, 0.15))


lv, lf = [], []
for yy in range(0, H, STEP):
    row = yy * W * 4
    x = 0
    while x < W:
        if px[row + x * 4 + 3] < 0.5:
            x += STEP
            continue
        x0 = x
        while x < W and px[row + x * 4 + 3] >= 0.5:
            x += STEP
        # image row 0 is the bottom (v 0)
        u0, u1, v0, v1 = x0 / W, x / W, yy / H, (yy + STEP) / H
        c = [top(u * cols * U, -(1 - v) * rows * U) for u, v in ((u0, v0), (u1, v0), (u1, v1), (u0, v1))]
        if all(c):
            b = len(lv)
            lv += [tuple(p) for p in c]
            lf.append([b, b + 1, b + 2, b + 3])
lme = bpy.data.meshes.new("Legends")
lme.from_pydata(lv, [], lf)
lme.update()
leg = bpy.data.objects.new("Legends", lme)
bpy.context.scene.collection.objects.link(leg)
leg.data.materials.append(ds.plastic("xe_legends", "1A1A1A", 0.4))

for ob in (caps, leg):
    ob.scale = (0.001, 0.001, 0.001)          # metres, as the C64 model
bpy.ops.object.select_all(action="SELECT")
bpy.context.view_layer.objects.active = caps
bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
bpy.ops.wm.save_as_mainfile(filepath=OUT, compress=True)
print("XEKB %s: %d caps triangles faces %d, %d legend quads" % (OUT, len(lay), len(faces), len(lf)))
