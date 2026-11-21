CHECKS += check-p2share
#| NES player 2's shared file (`src/p2share.h`), Diatom's frames out and p2d's pads in, the writer
#| and the reader on one block: a frame read back whole at its size, a pitch wider than its row,
#| 0RGB1555 and RGB565 widened so full is full, a torn slot skipped, nothing read before the magic
#| or into a short buffer, the three slots in turn, a frame too big refused; the header's offsets
#| the Python checks read it by, and SETPLAYER2's rule for a system's buttons (within the twelve
#| RetroPad bits B to R, the NES's 0x01FD when none is left) and players (2 to 4, else 2)
