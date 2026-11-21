CHECKS += check-p8fx-gl
#| your PICO-8's Shader and Border drawn: the shim (`src/p8fx/shim.c`, built from `mk/cross.mk`'s
#| `P8FX_SRC`) loaded with `LD_PRELOAD` over a stand-in that calls SDL as pico8_64 0.2.7 does
#| (`tools/p8fx-standin.c`), on SDL's offscreen video and its OpenGL ES 2 renderer on Mesa's
#| llvmpipe, each frame read back before the present: with no look file, Off, a shader of none, LCD
#| No Backlight, a border picture not there, OpenGL ES 2 only (`MESA_GLES_VERSION_OVERRIDE=2.0`)
#| and SDL's software renderer the very frame drawn without the shim; each shader, inside PICO-8's
#| rect, within 3 levels of Diatom's GLSL in C (`shaderdemo --image`) at Full Screen (preblit 3),
#| Sharp Pixels (preblit 1 and 2), under 100% and for a rect past the screen's edges, black round
#| it; Blur round SDL's own picture, never black and never brighter than the picture; a picture
#| border the picture; PICO-8's FPS line, which SDL draws after, as without the shim; a look
#| written while it runs drawn next. Linux only; elsewhere it says it skipped, and `make
#| check-p8fx-gl P8FX_DOCKER=1` runs it in `tools/p8fx-gl.Dockerfile`'s Ubuntu
