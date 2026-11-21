CHECKS += check-portout
#| the port's latest output at the foot of that screen, with plastron's splash's own code
#| (`splash/src/portout.h`): read as a terminal shows it (`\r`, `\r\n`, backspace, escapes, UTF-8,
#| ZyperOS's own lines passed over, in the middle of a line too), shown once the screen has been up
#| 5 s in a row, drawn over the screen's own box, cut with an ellipsis, turned as the Pixel 2's
#| panel is; and the launcher's letters for it in English and German, both themes, on the foot
#| under Zyper's frame, drawn once and read back the same (builds the host launcher; skips that
#| part without SDL2). `sh tools/portout-check.sh --shots DIR` shoots the screen as a stand-in
#| port's lines change, and at 4 s and 6 s
