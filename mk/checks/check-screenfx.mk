CHECKS += check-screenfx
#| ZyperOS's own screens' shader (src/screenfx.h): each old screen's picture within one level of
#| what tools/shaderdemo.c, Diatom's GLSL in C, makes of a white picture at a game's size, every
#| pixel, at 100%, 50% and 10%; on the Pixel 2's 640x480 Scanlines dims every other row, the LCD's
#| grid a row and a column of two, the CRT shows its lines and its grille, CRT Curved is black in
#| its corners; the masks kept (src/screenfx_draw.c): each made once however often it is asked for,
#| six kept, the one used longest ago let go, none for Off; and the time a picture takes on this
#| machine
