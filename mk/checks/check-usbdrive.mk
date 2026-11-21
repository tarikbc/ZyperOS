CHECKS += check-usbdrive
#| plastron's usbdrive (src/drive.c), the card as a USB drive: never exported while mounted or
#| held; an export ends on the computer's eject, the cable out for 1.5 s, POWER, or no computer in
#| a minute; the page's numbers (src/drivestat.c): the totals and speeds as the page shows them,
#| each way idle at 0 two seconds after its last sector, a pause that keeps the speed, both ways at
#| once, never redrawn while idle, a counter that goes back, the clock's wrap; and the atlas
#| (src/atlas.c), read as untrusted: a broken one refused (a missing glyph, a glyph taller than its
#| slots, rects that overflow or are inverted, a huge picture), a row from the left or
#| right-aligned, cut at its slot, the slot cleared first, and a layout that skipped the parser
#| still drawing only inside its slot; bright.txt read as untrusted (rungs 1 to 255 going up, the
#| level on them, the bar inside the picture); the handheld's buttons (src/drive.c drv_key):
#| FUNCTION and volume step the rungs and stop at the ends, each rung said to the side lights as
#| `level brightness RUNG TOP`, volume alone does nothing, B ejects at once unless the computer may
#| still be copying (then it asks: A ejects, B keeps): a write in the last 5 s, more than 1 MB
#| dirty, or the gadget's counters unreadable, which always asks; /proc/<pid>/io and
#| /proc/meminfo's Dirty read as the kernel writes them; across the clock's wrap; the side lights'
#| line (src/drivestat.c dst_lights_line): each way's bytes a second over the last look alone, a
#| way under 64 KB/s still, `move 0 0` once when both go still, a short look counted at its length,
#| a counter that goes back
