CHECKS += check-gpu-upright
#| every picture Diatom's GPU ports draw, the right way up on the panel: the shared presenting code
#| (`diatom/port/gles_*.h`) built as each port builds it (`diatom/test/upright_test.c`, each port's
#| `PRESENT_TOP_DOWN` read from its own file) and run on Mesa's llvmpipe with no display, each
#| frame read back as that device's display reads it: the TrimUI Brick's page of fb0 (a texture
#| under a framebuffer object, as `pages_start` makes it) from its first byte, the GKD Pixel 2's
#| window surface (a pbuffer) from its top row, its portrait panel turned. A frame at Nearest the
#| picture to the pixel; at Sharp, Sharp-Shimmerless, CRT, CRT Curved, Scanlines, LCD Grid and LCD
#| No Backlight, and under 100%, its bright quarter top left; a DS frame's top screen at the top; a
#| GPU core's frame, drawn GL's way up and top down, to the pixel; the Border's quarters, a notice,
#| the level bar, On-Screen Stats and the low-battery disc each in its place and nowhere else; a
#| capture of a frame and of a GPU core's frame the right way up. Without the Brick's
#| `PRESENT_TOP_DOWN` every one of them but the captures came out upside down on its pages (issue
#| #419). And the launcher's CRT Curved, drawn from a target texture as the Brick's `bend_tex` is
#| and bent with `sfx_draw_curved` (`tools/gpu-upright-bend.c`), on SDL's OpenGL ES 2 renderer, the
#| Brick's: its bright quarter stays top left. Linux only; elsewhere it says it skipped, and `make
#| check-gpu-upright UPRIGHT_DOCKER=1` runs it in `tools/p8fx-gl.Dockerfile`'s Ubuntu
