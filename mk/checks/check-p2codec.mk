CHECKS += check-p2codec
#| player 2's frame codec (`src/p2d/codec.c`): 100 frames round trip to the pixel, the first a KEY
#| with its palette and a still one under 200 bytes, a new color sends the palette, more than 256
#| colors refused and the next a KEY, a full palette history started over, a size change a KEY, a
#| KEY every 120 frames and bad messages refused; writes the fixtures `check-play` reads and prints
#| the encode time
