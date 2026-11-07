# Pixel 2 product asset

`pixel2.glb` is the website's static product model. It uses the supplied GKD
Pixel II dimensioned drawing (63 x 81 x 17 mm body) and matching six-view
reference. It is a visual reconstruction, not manufacturer CAD or a printable
part. Controls and screw heads extend beyond the body dimensions.

The rear pixel logo traces the supplied drawing, including its connected
frame and detached square.

The model includes a cut screen recess, cross-shaped D-pad bore, round button
bores, separate plastic caps, unequal shoulder keys with thin rear lips and deeper outer wraps, a round
three-dot MENU key, a separate diagonal POWER key, a two-ended volume rocker,
a card slit with a finger scallop, bottom ports, and fourteen recessed rear grooves. Material primitives separate green
anodized aluminium, plastic, glass, steel, markings and battery lenses.

## Rebuild

With Blender 5.2:

```sh
blender --background --python tools/build-pixel2-model.py -- --render
```

The script writes:

- `site/models/pixel2.glb`: self-contained glTF 2.0, metres, Y up, front +Z.
  No compression decoder or external textures are required.
- `site/models/pixel2-poster.png`: transparent fallback render using the same
  rest pose and the `systems.png` screenshot.
- `build/pixel2-model/pixel2.blend`: editable parts, materials and studio setup.
- `build/pixel2-model/{front,back,left,right,top,bottom,hero}.png`: review renders.

The GLB combines the housing and controls into material primitives to reduce
render calls. The Blender scene keeps each part separate. Its named `Screen`
mesh has 4:3 UVs; `site/pixel2.js` supplies the screenshot texture at runtime.

The preview has a fixed upright pose and an orthographic camera. Dragging
rotates the product. Arrow keys also rotate it. There is no pointer following, idle sway, bobbing or automatic return.
Reduced motion also stops the screenshot timer. Hidden pages and off-screen
canvases stop that timer; there is no continuous animation loop.

## Review

Compare all six face renders to the supplied orthographic drawings. Check
screen and control positions before evaluating lighting. On the right, the
three-dot circle is MENU; the black capsule below it is POWER. Check both
shoulder projections: thin lips at the rear, deeper wraps on the outer sides.
On the left, check both chevrons, the vertical TF CARD label and finger scallop.
On the bottom, check separate diagonal speaker slots, both ports and screw crosses. Check the hero on
both website backgrounds, then check desktop and phone layouts, drag,
keyboard operation, and the fallback with WebGL unavailable. A Blender render
proves the asset's appearance in that renderer, not browser lighting parity.

Rebuild the poster when the model, rest pose, or `systems.png` changes. The
regular static site build copies the checked-in assets without requiring
Blender:

```sh
sh mk/site.sh
```
