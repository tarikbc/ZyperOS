CHECKS += check-pico8-display
#| your PICO-8's Display default: a shelf never set plays it at Original Shape (its row says so, a
#| cart opens with `aspect`, no row written), fake08 on it Full Screen, a "stretch" from before
#| 1.1.0 let go of once with the marker kept, another shelf's change writing that shelf's row
#| alone, and Full Screen picked on the shelf's row opening with `stretch`; the in-game menu's
#| Display row over your PICO-8 (src/portmenu.h's `port_menu_display`, PICO-8 draws its picture
#| where it was told as it started): launch.sh's run_port with stand-ins, the restart file
#| (`/tmp/zyperos_port_restart`) and USR2 as the menu's start again sends them ending the program
#| with TERM and starting it again with the file's Display, once, portkeys and MP4's follower going
#| on through it and MP4 not stopped, the file gone; a word that is no mode's a plain Quit, a file
#| from before the start none, POWER while the program ends starting nothing; then the real
#| launcher in desk mode's dummy window as portkeys starts it (`--port-menu PICO8 CART`): Continue,
#| Display (the mode `display.PICO8` keeps), Controls and Quit, Left and Right stepping nothing, A
#| its list, another mode asking "Start the cart again?" with the mode named and the cursor on
#| Cancel, Cancel keeping nothing, the mode on asking nothing, Start Again exiting 14
#| (`PORT_MENU_RESTART`) with the mode kept and in the file, B Continue removing a file from
#| before, no Display row over OpenBOR, and `--display` (the Next Display hotkey's) asking for the
#| next mode round with no menu, Cancel 10 and Start Again 14; and On-Screen Stats after Display
#| (none over OpenBOR), Off, Right turning it On in the device's database and Left Off; every wait
#| in its walks a wait for an event, never for a time or a count of frames (tools/deskwalk.py: the
#| panel a key lands on, from desk's `state`, a setting or a file), and a drawn string found
#| wherever its line begins
