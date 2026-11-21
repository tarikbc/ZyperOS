CHECKS += check-battempty
#| the battery run flat while the handheld runs (src/battempty.h), as `battery_empty` and portkeys
#| read it every 5 s: the level at 2% or under on two readings in a row, as before, or the voltage
#| under the device's empty voltage (3.45 V on the Pixel 2) on three whatever the level says (a
#| worn battery's 43%); never on the cable, never with no battery read; one or two readings under
#| with one over between, a reading with no voltage, a voltage under 2.7 V (a bad reading) and a
#| device with none each start the run again; the level's rule named first when both hold, and a
#| long run never wraps
