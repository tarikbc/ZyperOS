CHECKS += check-pkkeys
#| the volume keys over a port: MENU+VOL is the backlight whichever device is read first, the
#| kernel's autorepeat is no step, a hold repeats on the launcher's clock, and the levels saved are
#| the ones pressed; and over your PICO-8, SELECT's hotkeys by Diatom's own chord rule
#| (tools/pkhot.c: SELECT then the button, the button just before SELECT, SELECT held long then the
#| button; not after the d-pad, not Quick Save, not the kernel's repeat, not a SELECT a resync
#| found down) and CTRL-R as portkeys writes it to its uinput keyboard (tools/pkkbd.c), into a
#| pipe; and the screenshot (tools/pkshot.c): PICO-8's own 128 x 128 out of a turned and a
#| landscape panel in every Display mode's rect (src/p8draw.h), the cut rows of Zoomed and Edges
#| Cut black, the PNG's chunks and pixels back through zlib, and the shot laid in the pop-up's
#| square with its corners rounded; what PICO-8 reads while portkeys holds the joypad for the
#| hotkeys (`pkhot_out`): every press at once, MENU never, a chord never (the button just before
#| SELECT taken back), SELECT alone given back as it is let go for two turns at least and taken
#| back after, the game's with the d-pad or held past 400 ms; and that pad's writes
#| (tools/pkvpad.c), one event a button that changed and a report, into a pipe
