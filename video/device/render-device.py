"""The tour video's Pixel 2: tools/build-pixel2-model.py's model, straight on,
with its screen cut out so the launcher's frames show through it.

blender --background BLEND --python video/device/render-device.py -- OUT_DIR [WIDTH]

BLEND is the editable scene tools/build-pixel2-model.py saves
(build/pixel2-model/pixel2.blend). Writes OUT_DIR/pixel2-front.png, the
product on a transparent ground in the site poster's studio light, and
OUT_DIR/pixel2-front.json, where its screen is in that picture, in pixels.
video/scripts/prepare-assets.sh runs it.
"""
import json
import sys
from pathlib import Path

import bpy
from bpy_extras.object_utils import world_to_camera_view

argv = sys.argv[sys.argv.index('--') + 1:]
out = Path(argv[0])
width = int(argv[1]) if len(argv) > 1 else 2000
out.mkdir(parents=True, exist_ok=True)

scene = bpy.context.scene
root = bpy.data.objects['GKD Pixel 2']
root.rotation_euler = (0, 0, 0)          # straight on, still
camera = scene.camera
camera.location = (0, -220, 0)
camera.rotation_euler = (1.5707963, 0, 0)
camera.data.type = 'ORTHO'
camera.data.ortho_scale = 109
scene.render.resolution_x = width
scene.render.resolution_y = round(width * 1.2)
scene.render.resolution_percentage = 100
scene.render.film_transparent = True
scene.cycles.samples = 96
scene.cycles.use_denoising = True
# The GPU when Blender has one (Metal on a Mac)
try:
    prefs = bpy.context.preferences.addons['cycles'].preferences
    prefs.compute_device_type = 'METAL'
    prefs.get_devices()
    for d in prefs.devices:
        d.use = True
    scene.cycles.device = 'GPU'
except Exception as e:  # the CPU, then
    print('render-device: CPU,', e)

# The screen: a hole, so the picture under the product shows
screen = bpy.data.objects['Screen']
hole = bpy.data.materials.new('Screen hole')
hole.use_nodes = True
nodes = hole.node_tree.nodes
nodes.clear()
holdout = nodes.new('ShaderNodeHoldout')
output = nodes.new('ShaderNodeOutputMaterial')
hole.node_tree.links.new(holdout.outputs[0], output.inputs['Surface'])
screen.data.materials.clear()
screen.data.materials.append(hole)

bpy.context.view_layer.update()
w, h = scene.render.resolution_x, scene.render.resolution_y
pts = []
for v in screen.data.vertices:
    p = world_to_camera_view(scene, camera, screen.matrix_world @ v.co)
    pts.append((p.x * w, (1 - p.y) * h))
xs = [p[0] for p in pts]
ys = [p[1] for p in pts]
rect = {'image': 'pixel2-front.png', 'width': w, 'height': h,
        'screen': {'x': min(xs), 'y': min(ys), 'w': max(xs) - min(xs), 'h': max(ys) - min(ys)}}
(out / 'pixel2-front.json').write_text(json.dumps(rect, indent=2) + '\n')
scene.render.filepath = str(out / 'pixel2-front.png')
bpy.ops.render.render(write_still=True)
print('render-device:', rect)
