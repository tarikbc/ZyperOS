CHECKS += check-inrepeat
#| no button read twice by `in_repeat` in one step in `src/`: a repeat is true once a frame, so the
#| second read of a held button is false, and "either is due, then which one" turned a held
#| direction round (the home, the in-game save slots, File Transfer's and the USB questions did);
#| each is read once (`in_repeat_axis`). A reading of the source, its rule tried on a bad and a
#| good snippet first
