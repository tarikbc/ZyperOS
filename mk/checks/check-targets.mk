CHECKS += check-targets
#| every texture the launcher draws into is made `PLAT_TARGET_FORMAT` (src/platform.h), a format
#| SDL's GLES2 renderer keeps unswapped: in an ARGB8888 one a video snap on the Pixel 2 came out
#| with red and blue swapped; `make yuvtarget` shows it with the device's own SDL, in Docker ; and
#| the Pixel 2's screen goes onto the panel opaque, made so and opaque again after a video's frame
#| (left blending after the first MP4 video, whatever the screen held at alpha 0 came out black, an
#| RGB texture among it) (a reading of the source; no SDL)
