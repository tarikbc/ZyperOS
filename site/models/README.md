# Pixel 2 product asset

`pixel2.glb` is the website's static product model. It uses the supplied GKD
Pixel II dimensioned drawing (63 x 81 x 17 mm body) and matching six-view
reference. It is a visual reconstruction, not manufacturer CAD or a printable
part. Controls and screw heads extend beyond the body dimensions.

The rear pixel logo traces the supplied drawing, including its connected
frame and detached square.

The model includes a cut screen recess, cross-shaped D-pad bore, round button
bores, separate plastic caps, unequal shoulder keys with thin rear lips,
rounded outer corners and open lower cutouts, a round
three-dot MENU key, a separate diagonal POWER key, a two-ended volume rocker,
a card slit with a finger scallop, bottom ports, and fourteen recessed rear grooves. Material primitives separate green
anodized aluminium, plastic, glass, steel, markings and battery lenses.

## Rebuild

With Blender 5.2:

```sh
blender --background --python-exit-code 1 --python tools/build-pixel2-model.py -- --render
```

The script writes:

- `site/models/pixel2.glb`: self-contained glTF 2.0, metres, Y up, front +Z.
  No compression decoder or external textures are required.
- `site/models/pixel2-poster.png`: transparent fallback render using the same
  rest pose and the `systems.png` screenshot.
- `site/models/pixel2-music.png`: the native player capture from
  `site/music/now-playing.png` on the same product.
- `build/pixel2-model/pixel2.blend`: editable parts, materials and studio setup.
- `build/pixel2-model/{front,back,left,right,top,bottom,hero,music}.png`: review renders.

The GLB combines the housing and controls into material primitives to reduce
render calls. The Blender scene keeps each part separate. Its named `Screen`
mesh has 4:3 UVs; `site/pixel2.js` supplies the screenshot texture at runtime.

The preview uses an orthographic camera and a slow turntable rotation.
A full turn takes about 42 seconds after a gentle speed ramp. The first turn
adds a small tilt over 1.4 seconds. Fixed studio lights and narrow reflection
strips create a moving glint on the metal. The model does not orbit or bob.
Hover, drag, and visible keyboard focus pause the turn and screenshot changes.
Dragging and the arrow keys rotate the product. The turn resumes from that
angle, with a gentle speed ramp, after pointer or keyboard focus leaves.
Touch release also resumes the turn unless visible keyboard focus remains.
There is no automatic return to the rest pose after user input.
Reduced motion and the page's Pause motion control stop all automatic motion.
Hidden pages and off-screen canvases stop it too. Each stop keeps the current
angle. Manual rotation remains available while automatic motion is off.
One animation loop controls the turn and the four-second screenshot changes.
The loop starts when the model becomes visible, including after a later scroll.
When automatic motion stops, rendering only occurs after input or resize.
The first screen always shows `systems.png` before the other screens load.

## Review

Compare all six face renders to the supplied orthographic drawings. Check
screen and control positions before evaluating lighting. On the right, the
three-dot circle is MENU; the black capsule below it is POWER. Check both
shoulder projections: thin lips at the rear, deeper wraps on the outer sides.
L1/R1 use a 3.1 mm outside corner radius and a 0.7 mm inside corner radius.
The curved housing pockets follow their L-shaped outlines with 0.2 mm clearance.
Each shoulder keeps a 2.6 mm main pad, then steps down underneath to a
1.25 mm rear lip over the last 1.8 mm of depth. L1/R1 also use the thinner
rear lip along the outer arm. The ledge is abrupt, with only a 0.12 mm
molded edge break. It is not a gradual thickness taper.
The case retains its original shape. The generator checks the underside
height on both sides of the step for all four caps before exporting.
L2/R2 use 0.81 mm upper corner radii and 0.405 mm lower corner radii.
Each L2/R2 cap is 7.4 mm wide and 9.6 mm deep. L1/R1 follow the longer outer
arms in the supplied photo reference.
The [TortOS Pixel 2 manual](https://tortos.games/manual#pixel-2) and its rear
photo supply the shoulder shape reference. The generator creates original
geometry; it does not copy the reference model or its photo textures.
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

## The page's handhelds

The landing page shows the handhelds in `site/devices.json`; `mk/site_devices.py`
builds the page's parts for them, and `make check-site-devices` builds the page
with made-up ones added. Each handheld here has its `.glb`, its poster and a
quarter-size thumbnail for the hero's row (`<id>-thumb.png`), all written by
its `tools/build-<id>-model.py`.

Both scripts light the product the same way: Blender's own studio light
(`studio.exr`, Poly Haven's studio_small_01, CC0) for its reflections, the
area lights for clean highlights, and a broad softbox behind the camera that a
metal face mirrors back. The colours are the makers' own, sampled from their
product photos: the Pixel 2's green face `#9bba4d` (GKD's six-view, as its
sellers show it), the Brick's purple `#564f78` (TrimUI's photo on trimui.com).
On the page, three.js lights the `.glb` with its own studio, and each
handheld's `model.studio` in `site/devices.json` (reflections, a tone on the
colours, the softbox, the clear coat) holds its face near that colour.

## TrimUI Brick

`brick.glb`, `brick-poster.png` and `brick-thumb.png` are the TrimUI Brick, built by
`tools/build-brick-model.py` from the published outline (73.2 x 109.9 mm; 11.8
deep above the back ledge, 19.9 below) and the key places measured for `src/device/brick_model.c`, the numbers it
draws the launcher's own Brick with. The product images those were measured on are reference
only: nothing is traced from them, and none of TrimUI's marks is on the model.
It is the purple Brick: the shell and keys purple, F1 and F2 light grey, as
TrimUI's photo shows them. Its shell is plastic, so no metal: a satin finish
under a clear coat gives it its highlights.

```sh
blender --background --python-exit-code 1 --python tools/build-brick-model.py -- --render
```

The poster uses the Pixel 2 poster's pose, studio and scale (8.807 px a
millimetre), 800 x 1100 for the taller Brick, with `res/readme/brick/systems.png`
on its screen. Its `Screen` mesh has the same 4:3 UVs; `site/pixel2.js` draws
the Brick's own captures on it.

## R36S

`r36s.glb`, `r36s-poster.png` and `r36s-thumb.png` are the R36S, built by
`tools/build-r36s-model.py` from the maker's product sheet (80 x 119 x 35 mm,
the sticks included) and the key places measured on straight-on photos of a
real one for `src/device/r36s_model.c` (`docs/devices/r36s.md` names them).
The photos are reference only: nothing is traced from them, and no maker's
mark is on the model. It is a visual reconstruction, not CAD: every number
the script marks GUESS was not measured, and a caliper's number replaces it.
It is the white R36S of the maker's product photos: a white shell, glossy
black keys and d-pad, A B X Y's letters in their four colours, the sticks'
black caps in light grey rings, the seam under the glass, the grille's dot
and the microphone's hole, one volume rocker, the ribs down the sides' lower
part, the panel and the ribbed grip on the back, and the shoulders as black
paddles angled out past it. The R36S is on no page of the site yet
(`site/devices.json` has no entry for it).

```sh
R36S_SCREEN=SHOT.png blender -t 4 --background --python-exit-code 1 --python tools/build-r36s-model.py -- --render
```

The poster's screen is `res/readme/r36s/systems.png`, the Pixel 2 poster's
home as the R36S draws it (`tools/readme-shots.py`'s `systems` shot with its
DEVICE set to `r36s`); `R36S_SCREEN` puts another 1024x768 shot there. The video's
straight-on render: `blender --background build/r36s-model/r36s.blend
--python video/device/render-device.py -- OUT_DIR 2000`, which writes
`r36s-front.png` and its `.json` at the Pixel 2's and the Brick's 22 px a
millimetre.
