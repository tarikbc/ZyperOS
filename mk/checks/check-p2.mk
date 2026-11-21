CHECKS += check-p2
#| the launcher's side of NES Player 2 (`src/p2.c`), on fakes for every process and protocol
#| action: Player 2 offered on the NES's core (`fceumm`) alone, with its buttons (0x01FD) and two
#| players, and only on a device that has it; the cable when a computer is on it, else the hotspot
#| when an adapter is in; the page's address on each link; Diatom's PLAYER2 answer read by its
#| pairs; on the hotspot `SETPLAYER2 on=1 buttons=0x01FD players=2`, the theme file and the
#| hotspot, and p2d (with `--dns`) only once the hotspot is up; on the cable the link,
#| zyperos.local (asked again while it would not start) and p2d at once; a refusal starts nothing
#| and leaves no link to close, and no answer in time is told `on=0` too; the stop in the reverse
#| order after any end of a game (p2d's SIGTERM, SIGKILL a second later if it stays, `on=0`, the
#| share's file, then the hotspot or zyperos.local and the cable's link), a second stop doing
#| nothing; the game's end saying nothing (no notice and nothing to Diatom from a tick after it,
#| nor when the share goes or p2d ends with 0); a notice as each guest's bit comes and goes, and
#| none for a bit past the system's players; a failure the tick meets (p2d or the hotspot gone)
#| said once and stopping only what stops at once; the theme file again on a theme change
