CHECKS += check-snapsize
#| A video snap of any shape (640x480, a Nintendo DS game's 320x480, a Game Boy game's, 534x480, a
#| width of 1, odd ones) published as MP4 publishes it, read into its texture and drawn on the
#| shelf and on a game's Cover and Video page: its own shape where it is drawn, a texture never
#| under 640x480 (the Pixel 2's GPU faulted on narrower ones), nothing out of bounds under UBSan
#| and, on a Mac, Guard Malloc (builds the host launcher's objects; skips without SDL2)
