CHECKS += check-panelgamma-sunxi
#| the TrimUI Brick's display's gamma table (src/panelgamma_sunxi.h), against a stand-in
#| `/dev/disp` with the disp2 driver's command numbers: Standard turns the table off and sends
#| none; a look sends its 256 entries (the shared math's at the floor, red, green and blue in the
#| TCON's 24 bits), then turns it on; Night Mode alone is a table; a refused table is never turned
#| on; and Saturation's steps in the display engine's units (25 to 75, 50 the picture as drawn)
