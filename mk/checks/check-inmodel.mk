CHECKS += check-inmodel
#| the input model, for every device profile through its own codes and sticks (`src/inmodel.h`,
#| `src/inengine.h`, `src/device/<id>_input.h`): the logical buttons' one table, each profile's
#| table whole (every code once, every printed button sent and every sent one printed), every
#| code's hand and press (MENU and the Pixel 2's L2 and R2 as they are let go), MENU's chords on
#| every key and moved, MENU+VOL, L2+R2, both hotkeys capture screens catching every key, both
#| sticks still at rest and each push its own button, and every tutorial lesson with every code,
#| stick direction and chord: exactly the right ones pass, every wrong one named as printed (FN
#| on the R36S), and the glyph lit for a press the button the answer saw; built with `-Werror`
#| (no SDL)
