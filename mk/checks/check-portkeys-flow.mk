CHECKS += check-portkeys-flow
#| The actual portkeys reader and loop with fake syscalls: poll races, pending release, overflow,
#| removal, noise, and clocks; and over a port, the power key and an empty battery (two readings 5
#| s apart at 2%, or three under the Pixel 2's 3.45 V empty voltage at 50%, off the charger), each
#| one USR1 to launch.sh and never one to init; every battery reading passed on for the low-battery
#| disc, the charger with it, and On-Screen Stats' vertical-blank file polled with the keys and
#| read only when ready; over your PICO-8, the joypad's events through the chord rule to one act
#| after the pass (none from another device, with no table, after the d-pad, for a SELECT a resync
#| found down, or for Quick Save), and Auto Off: its time with no key one USR1, the charger holding
#| it, a gap past IDLE_GAP_MS starting it again, the loop looking inside that gap, Never ending
#| nothing; and the pad PICO-8 reads, every joypad event handed to it in order and no other
#| device's, asked each pass, told of a resync and of the joypad gone, and no menu from a MENU let
#| go after MENU+START sent the port home.
