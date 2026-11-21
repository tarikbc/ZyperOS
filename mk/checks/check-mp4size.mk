CHECKS += check-mp4size
#| MP4's video picture of any shape (4:3 at 1440x1080, 480x360 and 320x240, 16:9, odd, tiny, the
#| largest, the size changing inside one file) published as the daemon publishes it and read into
#| the player's texture: a texture as wide as the frame's rows (a multiple of 128) and as high as
#| the picture, each plane fitting the Pixel 2's Mesa tile buffer as rows (it ran past it, a crash
#| or a green bottom edge, for 4:3 widths), the picture drawn whole from its own rectangle, its
#| shape kept, nothing out of bounds under UBSan and, on a Mac, Guard Malloc (skips without SDL2)
