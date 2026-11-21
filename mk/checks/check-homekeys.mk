CHECKS += check-homekeys
#| the home's d-pad held as a thumb holds it, in desk mode over its control socket, the cursor read
#| after each frame: on the Grid a held Down walks a row at a time to the last row and stops, a
#| held Up back to the first and stops, a held Left stops at the first system, on 24 systems (the
#| window scrolls) and on 8; on Spotlight a held Down jumps forward as taps of it do. Each button
#| is read once a frame (`in_repeat_axis`): a second read of a held one is false, which turned a
#| held Down round
