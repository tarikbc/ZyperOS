"""The tour video's handhelds: tools/build-pixel2-model.py's Pixel 2,
tools/build-brick-model.py's TrimUI Brick or tools/build-r36s-model.py's R36S,
straight on, with its screen cut out so the launcher's frames show through it.

blender --background BLEND --python video/device/render-device.py -- OUT_DIR [WIDTH]

BLEND is the editable scene the model's script saves
(build/pixel2-model/pixel2.blend, build/brick-model/brick.blend,
build/r36s-model/r36s.blend). Writes OUT_DIR/pixel2-front.png (or
brick-front.png, r36s-front.png), the product on a transparent
ground in the site poster's studio light, and its .json, where its screen is
in that picture, in pixels. Both at the same pixels a millimetre, so the two
stand side by side true to size. video/scripts/prepare-assets.sh runs it.
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
# Which handheld the scene holds: its name, the picture's height to its width,
# and the camera's frame, 109 mm over the Pixel 2's height (22 px a mm at 2000)
DEVICES = {'GKD Pixel 2': ('pixel2-front', 1.2, 109), 'TrimUI Brick': ('brick-front', 1.45, 109 * 1.45 / 1.2),
           'R36S': ('r36s-front', 1.4, 109 * 1.4 / 1.2)}
name = next(n for n in DEVICES if n in bpy.data.objects)
stem, tall, frame = DEVICES[name]
root = bpy.data.objects[name]
root.rotation_euler = (0, 0, 0)          # straight on, still
camera = scene.camera
camera.location = (0, -220, 0)
camera.rotation_euler = (1.5707963, 0, 0)
camera.data.type = 'ORTHO'
camera.data.ortho_scale = frame
scene.render.resolution_x = width
scene.render.resolution_y = round(width * tall)
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
rect = {'image': f'{stem}.png', 'width': w, 'height': h,
        'screen': {'x': min(xs), 'y': min(ys), 'w': max(xs) - min(xs), 'h': max(ys) - min(ys)}}
(out / f'{stem}.json').write_text(json.dumps(rect, indent=2) + '\n')
scene.render.filepath = str(out / f'{stem}.png')
bpy.ops.render.render(write_still=True)
print('render-device:', rect)
