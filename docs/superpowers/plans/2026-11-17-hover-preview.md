# Hover preview Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every option list whose choice can be seen or heard shows the choice under its cursor before A takes it, through one piece in `src/ds/`.

**Architecture:** `src/ds/hover.h` holds a list's `ds_hover` (show, undo, draw, tick, rest_ms, own_sound) and runs it with `ds_hover_run` (show once per choice the cursor comes to, undo on the kept one and on close). `menu_choice.hover` replaces `preview` and `focus`; `menu_pick_hover` replaces `menu_pick_preview` and `menu_pick_focus`. The runner calls `menu_style.at` after the keys and before the frame, so a show lands in the frame the cursor does; `menu_style.quiet_moves` lets a sound list play its own move.

**Tech Stack:** C (gnu11), SDL2, the host build (`make native`), checks in `tools/`, `make check`, `tools/pixel-gate.py`.

**Spec:** the map Tarik approved, `hover-preview-map.md` (design-system-4-review notes, 2026-11-17), with its five calls.

## Global Constraints

- A show writes no setting and no file: B, a close or POWER leave the card as it was (Tarik's "no settings/file write on hover").
- Undo always before a choice is applied, so the row steps it from the kept one (menu_choose's steps count from `cur`).
- A show runs once per cursor move, never every frame.
- Lists that preview today keep the same pixels (pixel gate on their shots).
- Strings in all 8 languages if any are added (none planned).
- Not in 1.0.6: branch `hover-preview` from main 4d2506a, its own PR, no merge before 1.0.6 ships.
- While the release runs: no Pixel 2; one heavy process (`CHECK_JOBS=1`), no full pixel gate until the coordinator says the Brick builds are done.

## Review Focus

- POWER or MENU pressed while a list shows a choice: the kept state comes back (undo in `pick_run` after `menu_run`, whichever way it returned).
- A on the shown choice: undo then the row's own steps; the setting saved is the chosen one, and the screen shows it with no flash (both in one pass).
- A list of a shader each system has its own of (`none`): every choice shows, the undo puts back each system's own.
- Language's show: the list's own words change under the cursor; the cursor and the list's width hold (`language_apply(.., false)`, measured again).
- Holding Down through Accent, Home Screen, Language or Sort By on a big shelf: one show per move, measured on the host now and on the Pixel 2 and Brick after 1.0.6.

---

### Task 1: The piece, the runner hook, the old previews moved (done)

**Files:** Create `src/ds/hover.h`, `src/ds/hover.c`, `tools/hover-check.c`; modify `src/menu.h`, `src/menu_pick.h`, `src/menu_panel.c`, `src/menu_runner.c`, `src/panel.h`, `src/main.c` (`menu_step_quiet`), `src/sysmenu_screen.c`, `src/ingame_screen.c`, `src/shots.c`, `Makefile` (`check-hover`).

**Interfaces (produced):**
- `typedef struct ds_hover { void (*show)(void *ctx, int k); void (*undo)(void *ctx); void (*draw)(void *ctx, int k, int x, int y, int w, int h); void (*tick)(void *ctx); unsigned rest_ms; bool own_sound; } ds_hover;`
- `void ds_hover_begin(ds_hover_run *r, const ds_hover *h, void *ctx, int kept, int at, unsigned now);`, `void ds_hover_at(ds_hover_run *r, int k, unsigned now);`, `void ds_hover_end(ds_hover_run *r);`, `bool ds_hover_quiet(const ds_hover *h);`
- `menu_choice.hover` (`const struct ds_hover *`), `menu_pick_hover(app*, const menu_style*, void*, const char*, const char *const*, const char *const*, const ds_hover*, int n, int ticked, int at)`
- `menu_style.at` (`void (*)(app*, void *ctx, int sel)`), `menu_style.quiet_moves`, `int menu_step_quiet(app*, const menu_row*, int n, int sel, int dir, in_button b)`

- [x] `make check-hover`: show once per choice, undo on the kept one, rest, tick, undo on close only for what was shown.
- [x] Night Warmth, Look, Shader, Strength, Display (shelf, MENU, game menu) moved onto hovers; `make native`, `make check-menus` pass.
- [x] Pixel gate on the list shots (menu-shader-list, menu-night-warmth-list, system-menu-display-list, gamemenu-display-list-*, screen-colors-look-*): 0 of 522 changed.

### Task 2: Shots draw what the cursor shows

**Files:** `src/shots.c` (`shot_list_draw` and the shot compose).

- [x] A shot of an open list with `--pick-at K` calls the hover's `show(K)` before the screen is composed and `undo()` after, so host shots draw the preview; a list with no show draws as today.
- [x] Twelve new shots (Task 9's list), each list with its cursor off the kept choice; all fit in every language.

### Task 3: No write on a hover (the guard)

**Files:** `src/db.c` (a write count), `src/shots.c` (`--hover-sweep`), `tools/hover-sweep-check.py`, `Makefile` (`check-hover-sweep`).

- [x] `--hover-sweep` builds every option page the rows report builds (MENU's tabs, a system's menu, Muse's, Theme, Screen Colors, the game menu, Menu Music, On-Screen Stats), and for each choice with a show: shows every choice, undoes, and checks the db write count is unchanged and the page's rows read the same as before. Prints `hover: <page> <row>: <n> shown, 0 written, same after`.
- [x] `check-hover-sweep` fails on any write or any row that reads differently after undo.

### Task 4: MENU's lists on the whole screen

Battery Icon, Home Screen, Night Mode (MENU > Display); Language, Time Zone (lists of their own).

- [x] Each: a show that sets the in-memory state only (the apply half of its key handler, split from the save), an undo back to the kept value read at begin; Language with `language_apply(a, l, false)`, Time Zone with the zone in memory for the corner clock only.
- [x] Sweep passes; shots of each list with the cursor off the kept one.

### Task 5: Theme's lists

Accent, Light Background, Dark Background (Display > Theme).

- [x] Show applies the look in memory with the choice (Light Background with Dark on: the light appearance while the list is open); undo the kept look and appearance.
- [x] Sweep passes; shots `theme-accent-list-*`, `theme-light-list-dark`.

### Task 6: The shelf's lists

Sort By, View (a system's menu), Show (Muse's).

- [x] Show re-sorts or re-lays the shelf behind in memory, keeping the focus on the same game; undo the kept order or view and focus.
- [x] Sweep passes; shots of each list off the kept one.

### Task 7: Sound lists

UI Sounds, Audio Channels (MENU > Sound); Volume, Track (Menu Music, Track with rest_ms 500).

- [x] UI Sounds: own_sound, show sets the voice in memory and plays the move in it. Audio Channels: own_sound, show routes the channels in memory and plays the move. Volume: the music's volume in memory. Track: plays the track after the rest; undo the kept track (from where it was, if the player can say).
- [x] Sweep passes (no write).

### Task 8: Pictures and the band

DS Screens and Quick Slot (game menu), a button's choices (Controls), On-Screen Stats' Corner, Edge, Opacity, Color.

- [x] DS Screens: draw the paused game in layout k as Display does. Quick Slot: draw slot k's saved picture and time, "Empty" when none (no new string if one exists). A button's choices: the handheld beside marks the button with the job under the cursor. On-Screen Stats: the page's band over the list, drawn with the choice under the cursor.
- [x] Shots of each; sweep passes.

### Task 9: Docs, checks, gate, PR

- [x] DESIGN.md 11.3 (what a list's cursor shows, one paragraph per kind, the table of lists), README's feature list.
- [ ] `CHECK_JOBS=1 make check`; the pixel gate on the shots this branch touches (the full gate once the Brick builds are done).
- [ ] Sheet for Tarik (real shots, light and dark), PR, report to the coordinator.

## As built (notes for review)

- Found while building: a brightness key during Language's list would have kept the shown language (tick_boot_theme reads i18n_lang), and a Time Zone shown would have rewritten the night's hours file. While any list shows a choice that is not kept (`ds_hover_showing`), `tick_theme` leaves those ticks for after the undo.
- `plat_night_preview(-1)` is new: no warmth, so Night Mode's Off shows while On is kept.
- `bgm_hear(file)` is new: a track heard now, the setting untouched; a chosen track ends it.
- `theme_show(ix)`: a theme on screen without the theme file (theme_init writes it).
- Split for showing without saving: `view_set(.., keep)`, `muse_show_only`, `shelf_sort_to(.., keep)`.
- Not swept: the Controls button list (its model lives with the in-game Controls tab); its show changes only the table in memory and its shots show it.
- DS Screens draws the screens it has from the paused frame (Diatom's preview in the layout on now); from Single the other screen is unlit.
