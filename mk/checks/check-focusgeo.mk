CHECKS += check-focusgeo
#| Highlight's marks on whole panel pixels (src/focusgeo.h, DESIGN.md 11.47): every pixel edge
#| drawn at the unit that lands cleanest through the Pixel 2's linear scaling of the 1024 x 768
#| picture (edges 0, 2 and 3 of each 5 pixels exact, the others a tenth out at most), the focused
#| cover, its ring's gap and the ring, and Bar's bar on whole pixels, a cell of any shape moved two
#| pixels at most, a unit a pixel (the Brick) changed by none of it, and the step back of the tiles
#| out of focus
