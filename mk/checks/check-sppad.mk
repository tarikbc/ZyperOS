CHECKS += check-sppad
#| the TrimUI Smart Pro's controls (`tools/sppad.h`, `tools/sppad_frame.c`), its two halves'
#| serial frames: a frame found whole wherever the reads cut it, junk and stray `0xFF` heads
#| passed over, a frame cut short dropped and the next one found, 200 frames with junk between
#| them in reads of 1 to 11 bytes all found in order, more frames than room counted as lost; the
#| 15 buttons as the GKD Pixel 2's key codes (A, B, X, Y as SOUTH, EAST, NORTH, WEST, the d-pad as
#| `BTN_DPAD_*`, L2 and R2 as `BTN_TL2` and `BTN_TR2`, MENU as `BTN_MODE`), every press and let-go
#| said, one a frame long too; a stick's centre learned from its first 8 readings, a 10% dead
#| zone, its range grown to the furthest reading seen, turned the right way and never past the
#| end; a half silent for 2 s
