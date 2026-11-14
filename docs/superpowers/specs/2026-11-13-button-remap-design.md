# Button remapping, in an in-game menu with tabs

## Intent

I want a player to change what each button does, for one game or for every
game of a system, from the in-game menu, and keep it on the card. The ask
came from the ZyperOS Discord (#ideas): "Change what each button does for
one system, or for one game, right from the in-game menu, and keep it on
the card." Nindzusek added the part that matters most to him: the d-pad as
the left analog stick on the bigger systems (Dreamcast, PSP, a few
PlayStation games), because the Pixel 2 has no stick.

While designing it I changed the in-game menu itself: it gets MENU's tabs
(L1 and R1 go round them, the same tab row), and one tab, **Controls**, is
where buttons are changed. The tutorial's 3D Pixel 2 stands in that tab
and lights the button each row is about, so nobody has to guess which cap
"R2" is.

## Goals

- **Change any button** (A, B, X, Y, L1, R1, L2, R2, START, SELECT) to any
  button the game's system has, to a turbo face button, or to nothing.
- **For this game or for every game of the system**, chosen in one row,
  with the game's own buttons winning over the system's.
- **The d-pad as the left stick** as one switch, per game, on the Nintendo
  64, Dreamcast, PlayStation and PSP.
- **Kept on the card**, in the library database, beside the other per-game
  and per-system settings.
- **Takes effect at once.** Continue, and the game already has the new
  buttons.
- **One look for every menu.** The in-game menu uses MENU's tab row, from
  one shared component, not a copy.
- **No game button lost by accident.** Giving a button what another button
  does swaps the two; a game button left with no button says so.

## Non-goals

- Remapping the d-pad's four directions one by one. The d-pad is the d-pad
  or the left stick, nothing in between.
- Remapping MENU, the volume keys or POWER. MENU stays unmappable, as
  Diatom already enforces.
- Moving the hotkeys (SELECT with a shoulder). They stay on the physical
  buttons whatever the map says.
- Remapping a port's or a "run:" emulator's buttons. Their in-game menu
  (Continue, Controls, Quit) stays as it is, with no tabs.
- A right stick, analog triggers, or partial deflection. The stick is full
  deflection, as Diatom's stick=dpad is today.
- Sharing maps between handhelds, or importing RetroArch remap files.

## Anti-goals

- **A remap that leaks into a game nobody changed.** Diatom already resets
  the map on every RUN; the launcher sends each game exactly the map in
  force for it.
- **A setting the player cannot find again.** A game with its own buttons
  says so in its Controls tab and on its details page on the shelf.
- **A dropped frame in the game because of the menu.** The handheld is
  drawn only in the menu, from a kept texture.
- **A second tab row in the code.** MENU and the in-game menu draw tabs
  with the same functions.

## What exists today

- **The Pixel 2's controls** (plastron's rk3326s-gkd-pixel2.dts, Diatom's
  port/pixel2.c keymap): a d-pad of four keys, A B X Y, L1 R1 L2 R2 (real
  keys), START, SELECT, FUNCTION (MENU), VOL+ and VOL-, POWER. No analog
  stick, no L3 or R3.
- **Diatom already remaps** (diatom/src/env.c `diatom_input_set_map`, the
  SETMAP verb): `from:to` pairs by canonical button name, `none`, turbo
  (`x:a~3`, half a cycle in frames), and one layer while one button is
  held (`select+a:a`, the N64's C buttons). Applied whole or not at all.
  SETMAP is part of the state plane, so it works while the game is paused
  in the menu. RUN resets the map to identity.
- **The launcher sends one map per game** (src/resident.c): the system's
  `map|` line from config/systems.cfg joined with its turbo
  (`turbo.<TAG>` in the library database, src/db.c defaults), after RUN.
  No screen edits either.
- **The d-pad as the stick** (Diatom's stick.h, RUN `stick=`):
  `stick|N64|dpad` always (SELECT, then the d-pad, is the N64's own
  D-pad), and `stick|PSP|toggle` (R2 turns it, the side kept per game in
  `stick.<TAG>.<file>`, src/main.c stick_get/stick_put). The Dreamcast and
  the PlayStation have none. The PlayStation runs pcsx_rearmed's DualShock
  (controller|PS1|517) in digital mode, its analog combo moved to L3+R3,
  which the Pixel 2 does not have.
- **The in-game menu** (src/game_menu.c, main.c game_menu): one list,
  Continue, Save, Load, Quick Slot, Display, Shader, (a DS game's Screens,
  Swap Screens, Stylus; an N64 game's Pak; an NES game's Player 2),
  Controls, Achievements, Reset, Quit. Controls opens read-only pages
  (DESIGN.md 11.28): the system's buttons by button, what a system adds,
  Hotkeys, In a game.
- **MENU's tabs** (DESIGN.md 11.3, main.c menu_tabs_draw, menu_tab_row,
  menu_tabs_bar): tied to MENU's own names (sys_menu_tab_name, PT_TABS).
- **The tutorial's handheld** (src/tourdevice.c): the Pixel 2 drawn in code
  from the landing page's model, turned, with keys lit and pressed, kept in
  a texture while it does not change. Tied to the tutorial's state
  (frame_of reads a `tour`).
- **Per-game and per-system keys** in the library database: `display.<TAG>`,
  `shader.<TAG>`, `quickslot.<TAG>`, `pak.<TAG>.<file>`,
  `stick.<TAG>.<file>`. Rename moves `pak.` but not `stick.` (a gap today).

## Approaches

Three directions were drawn as real launcher frames on 2026-11-13:

1. **A Buttons list.** A new in-game row opens one list: Applies To on
   top, one row per button, Left/Right changes a value in place, Reset at
   the end. Fast and familiar (Display and Shader work this way), but one
   more row in an already long menu.
2. **Controls you can edit.** No new row: the read-only Controls pages get
   a cursor, A on a button opens a list of choices, Y toggles "Just This
   Game". Keeps the menu short, but mixes editable and read-only pages and
   splits every paired row.
3. **Press to change.** Rows by the game's buttons, A and then press the
   handheld button to give it; the two swap; on the way out a question asks
   "this game or every game". Direct, but leaving needs MENU (every other
   button is a candidate) and the whole map is never in view.

**I picked none of the three as drawn.** The in-game menu becomes MENU's
tabs, and one tab, Controls, holds the editor: direction 1's list, inside
a tab, so the in-game menu gets no longer. Then seven refinements, all
taken: swap instead of copy; keep the tab and row while the same game is
played; mark a game's own buttons (Applies To in the accent ink, a row on
the details page); the Dreamcast's own d-pad under SELECT; a warning row
for a game button left with none; one Face Buttons row; and the tutorial's
handheld, on the Controls tab only, animated.

## Design

### 1. The in-game menu, in tabs

Three tabs, in the order L1 and R1 go round them, with MENU's tab row (the
L1 and R1 glyphs, the tab in **label** **text**, its neighbours in
**meta** **text-dim**, the **accent-ink** bar on the rule):

| Tab | Rows |
|---|---|
| **Game** | Continue, Save, Load, Quick Slot, Player 2 (NES), Achievements, Reset, Quit |
| **Display** | Display, Shader; a DS game's Screens, Swap Screens, Stylus |
| **Controls** | section 2 |

- Rows that exist today keep their behaviour and their place within the
  group. Pak moves to Controls. The Controls row and its read-only pages
  leave the Game tab; the pages live on behind the Hotkeys row (section 2).
- **X quits from any tab**, as it does from any row today (gm_x_row), and
  Quit keeps X's glyph.
- **B and MENU continue** the game, as now.
- **Where it opens:** the first MENU of a game opens on Game, on Continue.
  After that it opens on the tab and the row it was left on, until the
  game ends (Quit, POWER, or another game). Tuning a button is then
  change, MENU, try, MENU, and the cursor is still there.
- **The panel** is the same size on every tab and holds still as the tabs
  go round (11.3). It is wider than today's in-game menu: the content is
  860 units, so the panel stands 24 units from the screen's sides, the
  most the panel rule allows, to make room for the handheld.
- Over a port (OpenBOR, PortMaster) the menu stays as it is: Continue,
  Controls, Quit, no tabs.

### 2. The Controls tab

Rows, in order (those in brackets only where they apply):

1. **Applies To**: This Game, or `<System>`, every game of the system, by
   its name ("PlayStation", "Dreamcast"), or by its short label where the
   name does not fit the row beside Applies To (measured with the font:
   "PS1" in French). Left/Right and A switch it. It opens on This Game the
   first time in a game; after that it keeps its value with the tab and
   the row (section 1) until the game ends, so a player tuning PlayStation
   comes back to PlayStation *(set while writing this)*. Its value is in
   the **accent-ink** when this game has its own buttons.
2. [A dead row, "This game keeps its own buttons.", under Applies To,
   while it says `<System>` and this game has its own.]
3. [**D-pad as Stick**: On or Off. Always this game's, whatever Applies
   To says. Section 5.]
4. [**Pak**: an N64 game's, as today.]
5. [**Face Buttons**: By Letter or By Place. Section 6.]
6. **A, B, X, Y, L1, R1, L2, R2, START, SELECT**, one row each: the
   handheld's button at the left, what it does at the right, the core's own
   name for it (the names src/controls.c measured; PlayStation and PSP faces
   drawn as their shapes). Section 3.
7. [A dead row per game button left with no button by what the game is
   sent (a SELECT the stick's rule takes counts as SELECT), "× has no
   button now", right under the row whose change caused it: the button
   that gives no game button now (Nothing, a turbo), not the one that gave
   it by default.]
8. **Reset Buttons**: live only when the shown scope differs from its
   default. A asks first, on Cancel, in the confirm panel Delete Game uses:
   "Put back the PlayStation's buttons in Spyro the Dragon?" for This Game,
   "Put back ZyperOS's buttons for every PlayStation game?" for
   `<System>`.
9. **Hotkeys**: A opens the read-only pages that the Controls row opened
   before, less the system's buttons (this tab shows them): what a system
   adds (the N64's C buttons, the DS's screens), Hotkeys, In a game. They
   are the pages as they were, at their own width, with no handheld beside
   them (their rows do not fit beside it in every language), SELECT's note
   under the Hotkeys page's rule. The N64's C buttons page says which Pak
   is in ("Pak: Rumble Pak"), not where: the Pak row is this tab's.

A row that cannot change on this system is a dead row with its value: the
N64's SELECT ("C buttons"), the Dreamcast's SELECT while D-pad as Stick is
on ("Held: D-pad"), and a button Diatom keeps for itself, which no map
reaches: the PSP's R2 ("D-pad as stick", its turn) and a DS game's L2 and
R2 ("Other screen", "Stylus on/off") *(final review, 2026-11-13)*. A swap
never lands on a dead row: it keeps what it gives. What Diatom does with a
button is no choice.

The list goes round and scrolls as every list does (11.3), six rows in
view beside the handheld.

### 3. Changing a button

- **Left/Right** steps the row's value through its choices, in this
  order: the system's buttons (in src/controls.c's order for that system:
  faces, shoulders, Start, Select), then Turbo of each face button, then
  Nothing. Only buttons the system's core names are choices: a Dreamcast's
  list has A, B, X, Y, L trigger, R trigger, Start, Turbo A to Turbo Y,
  Nothing.
- **A** opens the same choices as a list of their own (the panel's heading
  "Y: PlayStation"), the handheld still beside it: a tick on what the
  button does now, "default" on the scope's default (a value in
  **text-dim**). A on a choice takes it and goes back; B goes back with no
  change.
- **Swap, not copy.** When button P is given game button G (a plain one,
  not Turbo, not Nothing) and another button Q already gives G, Q takes
  what P gave before. So Y given Cross makes B give Square, and both rows
  show it. Turbo and Nothing never swap: Y can be Turbo Cross while B stays
  Cross.
- **Left/Right steps do not pile up swaps.** Each step works from the map
  as it was when the cursor came onto the row, so stepping through the
  list leaves the other buttons as they were except for the one swap of
  the value it stops on. A picked choice (A) is one swap.
- **A changed value** (one that differs from the scope's default) is drawn
  in the **accent-ink**, as an earned achievement's value is (VCOL_EARNED).
  On the focus fill it is **on-accent**, as every value.
- **Every change is sent at once**: the launcher sends SETMAP with the
  whole table while the game is paused, so Continue plays with it.
- **A game button left with no button** (Nothing on the only button that
  gave it, and no turbo of it either) gets the warning row of section 2.
  A turbo of it counts as a button.
- **SELECT keeps the hotkeys' rule** whatever it gives. Diatom's chord
  (chord.h) gives a lone SELECT to the game as it is let go, or once held
  400 ms, and never under a shoulder; a SELECT set to Start or to Square
  reaches the game the same way, so SELECT+R1 never presses it in the game.
  chord.c does not change *(set while writing this)*.

### 4. This game, or every game of the system

- Two maps can exist per system: the system's (`map.<TAG>`) and a game's
  (`map.<TAG>.<file>`). A game uses its own if it has one, else the
  system's, else ZyperOS's default (the `map|` line and `turbo.<TAG>`, as
  today).
- **Applies To: This Game** shows the map in force for this game. The first
  change copies it into the game's own map; from then on the game's map is
  edited, and later changes to the system's map do not reach this game.
  Its default (for "changed" and "default") is the system's map in force.
- **Applies To: `<System>`** shows the system's map. Changes write
  `map.<TAG>` and reach every game of the system without its own map at
  its next start (and this one at once, if it has none). Its default is
  ZyperOS's.
- **Reset Buttons** deletes the shown scope's key: the game's (back to the
  system's) or the system's (back to ZyperOS's). In This Game it also puts
  D-pad as Stick back to the system's default.
- **No key that equals its default.** When a change leaves a scope's table
  equal to its default (Left and then Right again), its key is deleted, so
  undoing a change undoes "own buttons" too: no accent ink, no shelf row,
  and the system's later changes reach the game again *(set while writing
  this)*.
- **"Own buttons" is the map only.** D-pad as Stick is per game on every
  game and does not count: R2 on the PSP writes its side for every game
  played, and that is not the player choosing buttons. An N64 game with
  the stick turned off shows it in its Controls tab, where the switch is
  *(set while writing this)*.
- **How a game with its own buttons shows:** Applies To's value in the
  **accent-ink**; on the shelf, its details page (src/game_menu.c gi_rows)
  gets a **Buttons | This Game** row after Favorite, only for such a game.
  A on it opens the same Controls list as a page of its own (heading the
  game's name, no tab row, the handheld beside it), its Applies To fixed on
  This Game, with no game running: changes are kept for the next start.

### 5. D-pad as Stick

One switch per game, `stick.<TAG>.<file>` ("1" or "0"; absent is the
system's default), on the systems whose pad has a left stick and whose
core reads it:

| System | Default | With it on |
|---|---|---|
| Nintendo 64 | On | as today: SELECT, then the d-pad, is the N64's own D-pad |
| Dreamcast | Off | the same SELECT rule: the Dreamcast has no SELECT, so nothing is lost |
| PlayStation | Off | the plain stick, no SELECT rule (SELECT is the PlayStation's own); the d-pad stays the d-pad too, so a game that keeps its pad digital still moves |
| PSP | Off | as today, and R2 still turns it in the game; the row and R2 agree |

- `stick.<TAG>.<file>` is deleted when it equals the system's default, R2's
  turn on the PSP included (stick_put writes "0" today).
- On the Dreamcast with the stick on, the stick's SELECT rule wins over a
  SELECT pair in the map, for that game; the row shows "Held: D-pad".
- The Saturn is not on the list: its stick is the 3D Control Pad, a
  controller type of its own; the standard pad, which most games use, is
  all d-pad.
- **It takes effect at once.** Diatom gets a new verb in the state plane,
  `SETSTICK mode=dpad|toggle|toggle-stick|off`, with RUN's `stick=` meaning,
  answered by `STICK on=` as R2's turn is today. Additive: an old Diatom
  ignores it and the setting applies at the next start.
- A PlayStation game moves with the stick only in the DualShock's analog
  mode. Many games that need the stick turn it on themselves, and the core
  has a `dualshock_init_analog` setting and an "Auto-enabling dualshock
  analog mode" path. Which of those applies has to be measured on the
  Pixel 2 (Risks). If the analog mode comes only from a setting the core
  reads at its start, the row sets it per game at RUN, as the Pak's option
  is sent after the system's (plat_resident_opt), and on the PlayStation
  the row says the change applies from the next start, in the pop-up the
  stick already has. The PlayStation keeps the row either way; it is the
  system Nindzusek asked for *(set while writing this)*.
- The read-only Controls pages and the tab's D-pad line say what the d-pad
  is (the core's name for the stick), as the N64's page does today.

### 6. Face Buttons

One row that sets all four faces, offered where the caps' letters and the
system's buttons can disagree: **By Letter** (each cap gives the button it
names, ZyperOS's choice in systems.cfg today) or **By Place** (each cap
gives the button in its place on the original pad).

- Systems: those whose four faces are letters in src/controls.c and whose
  `map|` swaps them by letter today: Dreamcast, Saturn, Neo Geo Pocket and
  Pocket Color (the builder confirms the list from PADS and `map|`).
  Numbered and shaped faces (NAOMI, PlayStation) have no row.
- Left/Right and A switch it. It writes the four face pairs of the scope's
  map, as a swap of all four, so the other buttons stay.
- Its value reads Mixed when the four faces match neither layout (after a
  change to one face); a step from Mixed goes to By Letter.

### 7. The handheld

The tutorial's Pixel 2, on the **Controls tab only** and on a button's
choices opened from it. Not on Game or Display, and not on the Hotkeys
row's pages, which are at their own width (section 2).

- **Where:** inside the panel, in a column at the left of the rows, under
  the tab row. The rows and the focus fill start 280 units in from the
  content's left edge; the handheld is centered in the column (about 3.15
  units a millimetre), its left edge on the rule's left end, a clear gap
  between it and the fill. The panel keeps its size (section 1).
- **What lights:** the keys of the row under the cursor, in the tutorial's
  lit look (accent fill, ring, halo): a button row its button; D-pad as
  Stick the d-pad (and SELECT on the N64 and the Dreamcast, the way to
  their own d-pad); Face Buttons the four faces; Reset Buttons every
  changed button; a swap both buttons, while the cursor stays on the row.
  Applies To, Pak and Hotkeys light nothing. On a button's choices, the
  button.
- **Turn:** it faces front (the tutorial's front pose, 10 degrees). A lit
  shoulder stands up out of the top edge, as in the tutorial.

### 8. Motion

DESIGN.md 8's tokens and curves only; nothing overshoots.

| What | How |
|---|---|
| R1 or L1 into Controls | the handheld comes with the rows: 64 units in the press's way, faded up, in **move** (160 ms, out), and turns from 40 to 10 degrees in **turn** (300 ms) |
| Out of Controls | it leaves with the old tab's name: 96 units the press's way, gone in the first two fifths of **move** |
| Up or Down | the fill glides in **quick** (as today); the old key's light goes out and the new key's comes in, in **quick** (as the tutorial does) |
| Left or Right on a button | the key presses in **press** (80 ms in, 80 out); a swapped partner lights in **quick** |
| A shoulder row | the shoulder stands up in **quick** |
| A to a button's choices, and back | the handheld holds still while the list beside it changes |
| A on Hotkeys, and back | the handheld goes and comes with the panel, as a page with none opens over it |
| The menu opening on Controls | the handheld opens with the panel (96% to full, in **move**) |

Every motion is interruptible and tells the redraw clock when it ends
(8.2). The handheld is drawn into its kept texture when its view changes
and copied otherwise; drawn fresh it costs about 15 ms a frame on the
Pixel 2, so a turn or a light change may cost frames in the menu, never in
the game.

### 9. Storage on the card

All in the library database, beside `pak.` and `stick.`:

| Key | Value |
|---|---|
| `map.<TAG>` | the system's table, as the pairs in which it differs from ZyperOS's default (`map|` joined with `turbo.<TAG>`), in SETMAP's form (`b:y,y:b,r1:b~3`), after a version, `1;` |
| `map.<TAG>.<file>` | a game's table, the same form and the same base; `<file>` as the shelf names it, as `pak.` does |
| `stick.<TAG>.<file>` | "1" or "0", now for every system of section 5 |

- The launcher builds the table from ZyperOS's default and the game's
  pairs, else the system's, and sends it whole after RUN, as resident.c
  does now. A scope's whole table (what section 4 copies and compares) is
  the default with its pairs over it.
- **Pairs, not whole tables, so a later release's default still reaches
  the buttons a player never set.** A release that changes a system's
  `map|` keeps the player's pairs for the buttons they set and gives the
  rest the new default. A release that changes what a RetroPad name means
  to a core (a core's own layout) migrates or drops that system's stored
  pairs by the version *(set while writing this)*.
- The N64's SELECT layer pairs come from the default and are never stored;
  the rows do not edit them.
- **Rename** moves `map.<TAG>.<file>` and `stick.<TAG>.<file>` with the
  game (src/gamern.c; `stick.` is missing there today). **Delete Game**
  deletes them.
- A table Diatom refuses (it applies whole or not at all) is logged by
  both sides, as today, and the game plays with ZyperOS's default.

### 10. Shared components

- **Tabs:** one tab set type for MENU and the in-game menu (its names, its
  count, the tab on, the move), passed through `menu_style.tabs`;
  menu_tabs_draw, menu_tab_row and menu_tabs_bar take it instead of
  calling sys_menu_tab_name, and the neighbours shown either side are at
  most (count - 1) / 2, so three tabs show one each side. MENU's look and
  behaviour do not change.
- **The handheld:** tourdevice.c's frame_of splits into the theme's colors
  and a view (center, scale, turn, each key's light and press). The
  tutorial and the Controls tab both build views and draw them through one
  function, with the kept texture shared (its box sized per caller).
- **A picture beside the rows:** a menu_style slot like `header`, at the
  left of the rows (`side`, `side_w`): menu_panel sets the rows and the
  focus fill in by `side_w` and calls the drawer with the column's
  rectangle. Used by the Controls tab and its pages.
- **The remap rules are pure** (no SDL): the choices for a system, a step
  with its swap, Face Buttons' layouts, "no button" warnings, changed or
  default, and the table's text form. A check (`make check-remap`) holds
  them.

### 11. Copy

New strings in res/strings, in every language there: the tab names Game,
Display, Controls; Applies To, This Game; D-pad as Stick (the
Title Case of controls.dpad_stick); Face Buttons, By Letter, By Place,
Mixed; Nothing; Turbo %s (controls.turbo exists); default; "%s has no
button now"; "This game keeps its own buttons."; Reset Buttons and its two
questions; Buttons (the details row); "C buttons" (the N64's SELECT row),
"Held: D-pad"; "%s: %s" for a button's choices' heading; and the C buttons
page's Pak note says "Pak: Rumble Pak".

### 12. Docs and the site

- DESIGN.md: 11.28 rewritten for the tabs and the Controls tab; 11.3 says
  the in-game menu has tabs too; 11.10 says the handheld is shared; a new
  section for this design's rules (swap, scope, warning, motion).
- README and zyperos.com: the feature in the features list and a row in
  the comparison table, with new light-theme shots of the Controls tab
  (tools/readme-shots.py), in the same work.
- docs/guide.md: how to change a button, for one game or a system, and
  the d-pad as the stick.

## Risks and checks on the device

- **The PlayStation's analog mode.** Test an analog-only game (Ape Escape)
  and a game that takes both, with the row on: does the DualShock go
  analog by itself, through `dualshock_init_analog`, or not at all? If not,
  the row is not offered for the PlayStation until it does.
- **The menu's frame cost** with the handheld's turn and light changes on
  the Pixel 2 (px2-perf over the paused game).
- **SETMAP while paused** reaching a GPU core (Dreamcast, N64, PSP) at the
  next frame; SETSTICK the same.
- **Hotkeys untouched** after a remap of SELECT or a shoulder: SELECT+R1
  still saves.
- **A remapped START or SELECT** on a system whose soft reset needs them
  (A+B+START+SELECT): it follows the map, as the game sees it.

## Left to the builder

- The order of a system's choices where src/controls.c has no order of its
  own, and the RetroPad names for a system it does not know.
- The exact list of Face Buttons systems, from PADS and `map|`.
- The panel's measures within the rules above (column width, scale, gap),
  checked with the font at the Pixel 2's size, in every language
  (`--controls-fit` grows to the tab).
- Splitting the work into PRs (the shared tabs and handheld first, then
  the Controls tab, then the shelf row and the docs).
- Release: 1.0.5 (my call on 2026-11-13, after first saying 1.0.6).
