# Building DESIGN.md: the roadmap

**Spec:** `DESIGN.md` (approved by 2026-11-04), tokens in `tools/designdef.py`.

DESIGN.md covers seven subsystems that each ship and test on their own, so it
is built as seven stacked pull requests against `main` on tarikbc/ZyperOS,
each on top of the one before, each with its own plan in this folder written
before it starts. A plan for a later phase is written after the phase before
it lands, from what that phase taught.

| # | PR | Builds | Depends on | Plan |
|---|---|---|---|---|
| 0 | #8 design system | DESIGN.md, tokens, check-design, mock kit | - | done |
| 1 | #7 bar on tokens (+ tarikbc/diatom#1) | the bar's colors, motion and theme from `src/design.h`; the theme file every process reads | 0 | `2026-11-04-bar-on-tokens.md` |
| 2 | themes | Beard and Salt & Pepper on every screen; MENU > Appearance; scrims, frame, radii, focus fill, lift | 1 | `2026-11-04-themes.md` |
| 3 | Lexend and card art | `menu.ttf`/`title.ttf` in Lexend; the 25 Plain Jane cards redrawn in Lexend for both themes; the loading loop's Beard frames | 2 | written after 2 |
| 4 | motion | the snappy durations and curves everywhere; menus opening and closing, one panel at a time | 2 | written after 3 |
| 5 | sounds | `src/uisound.c`: the six DESIGN.md sounds on speaker, jack and DAC; MENU > UI Sounds | 2 | `2026-11-04-ui-sounds.md` |
| 6 | Zyper in the menus | the companion in the ZyperOS menu's heading: breathe, hop, tilt | 4 | written after 5 |
| 7 | tutorial | src/tour.c (logic, started) and src/tourdraw.c; first launch; MENU > Show Tutorial | 3, 4, 5, 6 | written after 6 |

Each PR: `make check` green, host shots of every screen it touches in both
themes in its description, and the device test left to the main session
(it builds plastron and runs the Pixel 2).
