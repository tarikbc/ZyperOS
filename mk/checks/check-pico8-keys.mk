CHECKS += check-pico8-keys
#| your PICO-8's hotkeys, Reset and buttons as the launcher and PICO-8.sh do them (src/portmenu.h,
#| src/p8pad.h): PICO-8.sh against a stand-in pico8_64 exporting the pad's mapping line the
#| launcher left (`/tmp/zyperos-pico8-pad`) and the one that ships with none, another pad's or one
#| with what no mapping has; then the real launcher in desk mode's dummy window, `hotkeys.PICO8`
#| and `map.PICO8` in its library: a cart opened (px2-open's channel) leaving portkeys the hotkeys
#| in force in Diatom's RUN form and `Screenshots/PICO-8/<cart>` (`/tmp/zyperos-port-keys`),
#| PICO-8.sh the swapped line, "Screenshot saved" in both themes with an empty square and its
#| `SLOT` trailer, and with Next Shader on a key "Shader / <name>" for each of PICO-8's six shaders
#| in both themes; the menu over it (`--port-menu PICO8 CART`) with Reset between Controls and
#| Quit, asking "Reset the game?" with the cursor on Cancel, Reset exiting 13 (`PORT_MENU_RESET`);
#| `--reset` (the hotkey's) asking with no menu, Cancel 10, Reset 13; Controls opening the buttons
#| (Applies To, This Game, the A cap giving O), a step kept as `map.PICO8.<cart>`, B writing the
#| new line and asking "Start the cart again?" for the new buttons, Cancel the menu again and the
#| hotkeys written again as it ends, the folder kept, Start Again exiting 14 with the Display on in
#| the restart file; no Reset row over OpenBOR. PICO-8.sh exports the joypad's line and portkeys's
#| pad's (`06000000595a…`, the same body), each the shipped one when the file has none for it, and
#| sets `merge_joysticks 2` in config.txt, its other lines kept. portkeys's half is
#| `check-pkkeys`'s and `check-portkeys-flow`'s; every wait in its walks a wait for an event, never
#| for a time or a count of frames (tools/deskwalk.py: the panel a key lands on, from desk's
#| `state`, a setting or a file), and a drawn string found wherever its line begins
