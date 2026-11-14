# Button Remapping Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The in-game menu gets MENU's tabs (Game, Display, Controls); the Controls tab changes what each button does, for one game or a whole system, kept on the card, with D-pad as Stick, Face Buttons and the tutorial's handheld lighting the focused button.

**Architecture:** Pure rules in new C files (remap tables, the Controls tab's rows) with host checks; the launcher (src/main.c) runs the tab and talks to Diatom over the existing resident protocol (SETMAP live, a new SETSTICK); MENU's tab row and the tutorial's handheld become shared code used by both MENU/tutorial and the in-game menu.

**Tech Stack:** C (gnu11), SDL2 launcher (host build `make native`), Diatom libretro frontend (diatom/), SQLite library db, Python check scripts, res/strings + tools/i18n.py.

**Spec:** `docs/superpowers/specs/2026-11-13-button-remap-design.md` (read it whole before any task; section numbers below are its).

## Global Constraints

- Release: 1.0.5. The branch `button-remap` merges into main before the v1.0.5 tag; the session that holds the release decides when.
- No device use: the Pixel 2 is held for the release. Host build, host shots, Diatom's stub-core tests only. Never run zyperos.elf over SSH.
- Never commit to main. Commits on `button-remap`; no Co-Authored-By trailers. Public text (commits, PR, docs) in Tarik's first person ("I").
- MENU's look and behaviour do not change: its tab shots before and after the shared-tab task are byte-identical.
- The tutorial's frames do not change: its shots before and after the handheld task are byte-identical.
- Motion uses DESIGN.md 8 tokens only (press 80, quick 120, move 160, turn 300; curves out, turn); nothing overshoots.
- Every new string in all eight languages res/strings carries (en, zh-Hans, es, pt-BR, fr, de, ja, it); `make strings` regenerates src/i18n_ids.h and src/i18n_tables.c; `make check-strings` passes.
- No em dashes in any copy the player reads.
- Layout numbers from the approved mocks: in-game panel content width 860 on every tab; Controls rows and focus fill set in 280 from the content's left; handheld about 3.15 units a millimetre, front pose 10 degrees, left edge on the rule's left end.
- Card keys: `map.<TAG>`, `map.<TAG>.<file>` = `1;` + SETMAP pairs over ZyperOS's default (`map|` joined with `turbo.<TAG>`); `stick.<TAG>.<file>` = `1`/`0`, deleted when equal to the system default. No key is kept equal to its scope's default.
- `make check` (the parallel runner) passes at the end of every task; a new check gets its row in docs/building.md (check-building-doc holds it).

## Review Focus

- **A stored value from a newer or broken version** (`2;...`, a bad pair, an unknown button): the game plays with ZyperOS's default for that scope and the launcher logs it once; no crash, no half table. Test in Task 3 (`stored_bad_version_falls_back`, `stored_bad_pair_falls_back`).
- **The N64's SELECT layer after a remap**: changing A on the N64 keeps `select+a:a,select+b:b,select+y:y` in the table sent, so the C buttons still work. Test in Task 3 (`n64_layer_survives_change`).
- **SELECT remapped, hotkeys used**: SELECT given to Start, SELECT+R1 still saves and the game never sees Start. Test in Task 5 (diatom hotkeys.py case `select_remapped_hotkey`).
- **Rename and delete of a game with its own buttons**: `map.` and `stick.` keys move with a rename (file names with spaces and dots) and go with a delete. Test in Task 9.
- **A Diatom that does not know SETSTICK** (older resident): the launcher's change is kept and applies at the next start; nothing hangs waiting for STICK. Test in Task 5 (proto parse of an unknown verb stays ignored) and Task 7 (no wait on STICK).

---

### Task 1: One tab row for MENU and the in-game menu

**Files:**
- Modify: `src/main.c` (struct menu_tabs near line 5340; menu_tab_row, menu_tabs_draw, menu_tabs_bar near 5500-5590; MENU's L1/R1 handler near 18744; zyperos_menu_width near 14880)
- Modify: `src/panel.h` (menu_style.tabs comment)

**Interfaces:**
- Produces: `struct menu_tabs { int at, from, dir; unsigned t0; int count; const char *(*name)(int i); };` and `static void tabs_turn(menu_tabs *t, int dir)` (sets from, at = (at + dir + count) % count, dir, t0 = anim_now() or 1, and g_menu_epoch++). menu_tab_row and menu_tabs_draw read names and count from the tabs, never sys_menu_tab_name; the neighbours shown either side are `min(MENU_TAB_SIDE, (count - 1) / 2)`.
- MENU's g_tabs gets `.count = PT_TABS, .name = mpt_name` (a wrapper over sys_menu_tab_name).

- [ ] **Step 1: Shoot MENU's tabs before the change.** `make native`, then for each tab t in 0..5 and each theme: `build-native/zyperos --shot /tmp/before-$t-$th.png --theme $th --menu ... --tab $t` (use the flags tools/menushots-check.sh uses for MENU's tabs, plus one `--tab-from` mid-move frame). Keep them in build-native/tabs-before/.
- [ ] **Step 2: Make the change** as the Interfaces say; MENU's handler calls tabs_turn.
- [ ] **Step 3: Shoot again and compare.** Same commands into build-native/tabs-after/; `cmp` every pair. Expected: all identical.
- [ ] **Step 4:** `make check` passes (check-menushots, check-menu included).
- [ ] **Step 5: Commit** "Tabs: one tab row for any set of tabs, MENU's first".

### Task 2: The handheld as a view anyone can draw

**Files:**
- Modify: `src/tourdevice.h`, `src/tourdevice.c`
- Test: tools/tour-shot-check.py (existing) before/after

**Interfaces:**
- Produces (tourdevice.h):
  - `typedef enum { TD_DPAD, TD_A, TD_B, TD_X, TD_Y, TD_SELECT, TD_START, TD_MENU, TD_VOLUP, TD_VOLDN, TD_POWER, TD_L1, TD_R1, TD_L2, TD_R2, TD_KEYS } td_key;` (replaces the private K_ enum, same order)
  - `typedef struct { float cx, cy, scale, deg, op; float on[TD_KEYS], press[TD_KEYS]; float arm[4]; bool tabs; float tab_ring; } td_pose;`
  - `typedef struct { SDL_Texture *tex; SDL_Rect box; td_pose kept; bool ok; int can; } td_keep;` (box: the screen rectangle the kept texture covers)
  - `void td_draw(SDL_Renderer *r, td_keep *k, const td_pose *p);` draws from the kept texture when p equals k->kept, else draws (into the texture when the renderer can, as tourdev_make does)
  - `void td_keep_free(td_keep *k);`
- frame_of becomes: the tutorial's pose (`static void tour_pose(const tour *t, uint32_t now, float op, td_pose *p)`) and one colors function (`static void td_colors(view *v)`) shared by both paths. tourdev_make/tourdev_draw/tourdev_free keep their signatures and use a static td_keep with box {0, 80, 304, 368}.

- [ ] **Step 1: Shoot the tutorial before.** `python3 tools/tour-shot-check.py` writes its frames; copy them aside.
- [ ] **Step 2: Split as the Interfaces say.** No visual change.
- [ ] **Step 3: Shoot again and compare** every frame with `cmp`. Expected: identical.
- [ ] **Step 4:** `make check` passes.
- [ ] **Step 5: Commit** "Tutorial: the handheld as a pose any screen can draw".

### Task 3: The remap rules, pure

**Files:**
- Create: `src/remap.h`, `src/remap.c` (no SDL, no db)
- Modify: `src/controls.h`, `src/controls.c` (choices, labels and the letter layout come from PADS)
- Create: `tools/remap-check.c`; Modify: `Makefile` (check-remap in CHECKS, its build rule like controls-check's), `docs/building.md` (its row)

**Interfaces:**
- Produces (remap.h):
  - `#define RM_BUTTONS 10` the handheld's buttons in order a b x y l1 r1 l2 r2 start select (`RM_A`..`RM_SELECT`)
  - Targets are Diatom's canonical names' indices (`up down left right a b x y l1 r1 l2 r2 select start`), `RM_NONE` (-1) for nothing.
  - `typedef struct { signed char to; unsigned char turbo; } rm_choice;` (turbo: half-period in frames, 0 plain; turbo choices use 3)
  - `typedef struct { rm_choice b[RM_BUTTONS]; char layer[160]; } rm_table;` (layer: the `held+from:to` pairs verbatim, never edited)
  - `bool rm_parse(const char *spec, rm_table *out);` SETMAP form over identity, later pair wins (as Diatom); false on any bad pair
  - `void rm_format(const rm_table *t, char *out, size_t n);` whole table, SETMAP form, `identity` when nothing differs
  - `void rm_store(const rm_table *def, const rm_table *t, char *out, size_t n);` `1;` + pairs where t differs from def; `""` when equal
  - `bool rm_load(const rm_table *def, const char *stored, rm_table *out);` def with the pairs over it; false and out = def for another version or a bad pair
  - `bool rm_equal(const rm_table *a, const rm_table *b);`
  - `bool rm_changed(const rm_table *def, const rm_table *t, int button);`
  - `void rm_set(rm_table *t, const rm_table *base, int button, rm_choice c);` the swap rule of spec §3 worked from `base` (the table when the cursor came onto the row): a plain c whose target another button Q gives in base makes Q take button's base choice; turbo and none never swap
  - `int rm_unreached(const rm_table *def, const rm_table *t, int *targets, int max);` targets def reaches (plain or turbo) that t reaches nowhere
  - `void rm_faces(rm_table *t, const rm_table *layout);` writes the four face buttons from layout, leaving the rest
  - `int rm_faces_of(const rm_table *t, const rm_table *letters);` 0 By Letter, 1 By Place (identity faces), 2 Mixed
- Produces (controls.h):
  - `int ctl_remap_choices(const char *tag, const char *ext, rm_choice *out, int max);` the system's labelled targets in PADS order (faces, shoulders, Start, Select), then Turbo of each labelled face, then none
  - `void ctl_remap_say(const char *tag, const char *ext, rm_choice c, char *out, size_t n);` the core's name (shapes for PS1/PSP), "Turbo %s" (controls.turbo), or the Nothing string
  - `bool ctl_letter_layout(const char *tag, rm_table *letters);` true where at least two faces carry letters in PADS and the letter layout differs from identity (expected true: DC, SATURN, NGP, NGPC; false: PS1, PSP, NAOMI, NES)

- [ ] **Step 1: Write the failing checks** in tools/remap-check.c, each a function printing PASS/FAIL like controls-check:
  - `parse_format_roundtrip`: `rm_parse("a:b,b:a,x:y,y:x,l1:l2,l2:l1,r1:r2,r2:r1")` then rm_format gives the same pairs.
  - `swap_y_cross`: PS1 identity; `rm_set(&t, &base, RM_Y, (rm_choice){B,0})` → t.b[RM_Y].to == B and t.b[RM_B].to == Y.
  - `turbo_never_swaps`: Y given Turbo B → b[RM_B].to still B.
  - `steps_do_not_pile`: from base, set Y to B then (from the same base) Y to X → b[RM_B] back to B, b[RM_X] is Y's base (Y).
  - `none_warns`: B set to none on identity → rm_unreached lists B; with Y = Turbo B, nothing listed.
  - `store_equal_is_empty`: rm_store(def, def) == "".
  - `store_load_roundtrip`: changes over the DC default survive rm_store then rm_load.
  - `stored_bad_version_falls_back`: rm_load(def, "2;a:b") false, out equals def.
  - `stored_bad_pair_falls_back`: rm_load(def, "1;a:zz") false, out equals def.
  - `n64_layer_survives_change`: the N64 default from config/systems.cfg, A changed, rm_format still holds `select+a:a,select+b:b,select+y:y`.
  - `letters_dc_place_identity`: ctl_letter_layout("DC") true; rm_faces with identity faces gives a:a..y:y; rm_faces_of reads 1; one face changed reads 2.
  - `choices_dc`: ctl_remap_choices("DC") names A B X Y "L trigger" "R trigger" Start, Turbo A..Turbo Y, Nothing (spec §3).
- [ ] **Step 2:** `make check-remap` fails (no remap.c).
- [ ] **Step 3: Implement** remap.c and the three controls.c functions.
- [ ] **Step 4:** `make check-remap` passes; `make check` passes.
- [ ] **Step 5: Commit** "Remap: the rules, pure, and their check".

### Task 4: Kept on the card, sent to the game

**Files:**
- Create: `src/remapstore.h`, `src/remapstore.c` (db_lib; no SDL)
- Modify: `src/main.c` (stick_key/stick_get/stick_put near 780; the launch near 23350 that calls cfg_run_keys and plat_resident_map; ctl_game's map/turbo for Controls pages)
- Modify: `src/resident.c` (stop joining turbo: the launcher sends the whole table), `src/platform.h` (plat_resident_map's comment)
- Modify: `config/systems.cfg` (stick lines, below), `src/config.c` (the optional 4th field)
- Test: tools/remap-check.c (db-backed cases with a temp db, as controls-check links db.c)

**Interfaces:**
- Consumes: Task 3's rm_* functions.
- Produces (remapstore.h):
  - `void rms_default(const system_cfg *s, rm_table *out);` rm_parse of `s->map` then plat_turbo_map's pairs (later wins, as resident.c joins them today)
  - `bool rms_system(const system_cfg *s, rm_table *out);` default + `map.<TAG>`; returns whether the key exists
  - `bool rms_game(const system_cfg *s, const char *file, rm_table *out);` default + `map.<TAG>.<file>` if it exists (true), else rms_system's table (false)
  - `void rms_put(const system_cfg *s, const char *file, const rm_table *t);` file NULL: the system's. Stores rm_store against ZyperOS's default; deletes the key when t equals the scope's default (the game's: rms_system's table; the system's: rms_default)
  - `void rms_reset(const system_cfg *s, const char *file);` deletes the scope's key
  - `bool rms_stick_offered(const system_cfg *s); bool rms_stick_default(const system_cfg *s); bool rms_stick_get(const system_cfg *s, const char *file); void rms_stick_put(const system_cfg *s, const char *file, bool on);` (put deletes when on == default; replaces main.c's stick_get/stick_put)
  - `void rms_run_stick(const system_cfg *s, bool on, char *key, size_t n);` the RUN key: `stick=dpad`/`stick=plain`/`stick=toggle`/`stick=toggle-stick`, or "" for none
- systems.cfg: `stick|N64|dpad`, `stick|DC|dpad|off`, `stick|PS1|plain|off`, `stick|PSP|toggle` (toggle: on means the side is the stick). The comment block above them says the 4th field is the default.
- The launch sends `rm_format(rms_game(...))` through plat_resident_map; resident.c sends it as given (no turbo join). The Controls pages get `.map = that table, .turbo = NULL`.
- A game whose stick is on with the SELECT rule (N64, Dreamcast) is sent its table with SELECT's pair back to identity: the stick's SELECT rule wins (spec §5).

- [ ] **Step 1: Failing checks:** `game_key_deleted_when_equal_system` (put a game table equal to the system's → no `map.PS1.Spyro.chd` row), `system_key_deleted_when_equal_default`, `game_copies_system` (system has B/Y swapped, game changes R1 → stored game pairs hold the swap and R1), `stick_default_n64_on_dc_off`, `stick_put_default_deletes`, `run_stick_keys` (DC on → stick=dpad, PS1 on → stick=plain, PSP on → stick=toggle-stick, N64 off → "").
- [ ] **Step 2:** fails. **Step 3:** implement; move main.c's stick helpers to rms_*; change the launch and resident.c.
- [ ] **Step 4:** `make check` passes, check-controls and check-controls-shots included (the Controls pages read the same table as before for an untouched card: identical shots).
- [ ] **Step 5: Commit** "Remap: kept on the card per system and per game, sent whole".

### Task 5: Diatom: SETSTICK and the plain stick

**Files:**
- Modify: `diatom/src/proto.c` (parse SETSTICK mode=), `diatom/src/diatom.h` (DIATOM_MSG_SETSTICK, its field), `diatom/src/main.c` (state plane case near 1719; RUN stick=plain near 3120 and 3292; stick_emit), `diatom/src/env.c` (nothing new expected: plain = diatom_input_set_stick_toggle(true) without main.c's R2 flip)
- Test: `diatom/test/proto.py`, `diatom/test/stateplane.py`, `diatom/test/hotkeys.py`; `diatom/README.md` protocol table

**Interfaces:**
- Produces: `SETSTICK\tmode=dpad|plain|toggle|toggle-stick|off` in the state plane (works paused or running), answered by `STICK\ton=0|1`; RUN `stick=plain`. An unknown mode is an ERROR line, the stick unchanged.

- [ ] **Step 1: Failing tests:** proto.py parses SETSTICK with each mode; stateplane.py: after RUN with no stick, `SETSTICK mode=plain` then a held RIGHT reads left-analog x = 0x7fff and no joypad RIGHT; `mode=off` back to joypad; `mode=dpad` then SELECT then RIGHT gives joypad RIGHT (the SELECT rule); hotkeys.py `select_remapped_hotkey`: SETMAP `select:start`, SELECT held + R1 → HOTKEY save, the core never reads START.
- [ ] **Step 2:** `make -C diatom check` fails on them.
- [ ] **Step 3: Implement.**
- [ ] **Step 4:** `make -C diatom check` passes.
- [ ] **Step 5: Commit** "Diatom: SETSTICK, and a plain stick for a pad with its own SELECT".

### Task 6: The in-game menu in tabs

**Files:**
- Modify: `src/game_menu.h`, `src/game_menu.c` (tabs and their rows), `src/main.c` (gm_ctx, gm_menu_build, gm_key, game_menu near 22600, gm_width, the `--gamemenu` shot flag gains `--gm-tab N`)
- Test: `tools/menu-check.c`

**Interfaces:**
- Produces (game_menu.h):
  - `typedef enum { GT_GAME, GT_DISPLAY, GT_CONTROLS, GT_TABS } gm_tab;` and `const char *gm_tab_name(int t);`
  - `int gm_tab_rows(const gm_ui *u, gm_tab t, gm_row *ids);` Game: CONTINUE SAVE LOAD QUICK PLAYER2 CHEEVOS RESET QUIT; Display: DISPLAY SHADER SCREENS SWAP STYLUS; Controls: none (its rows are Task 7's). GM_CONTROLS and GM_PAK leave the gm lists (Pak moves to Task 7). A port's menu (u->port) stays Continue, Controls, Quit with no tabs.
- main.c: gm_ctx gains `menu_tabs tabs` (count GT_TABS, name gm_tab_name) and the per-game memory `{ int tab; int row[GT_TABS]; bool scope_all; }` reset when a game starts, kept across MENU presses until it ends (spec §1, §2 Applies To). menu_style.tabs points at it; fixed_w is 860. L1/R1 call tabs_turn (Task 1). X quits on every tab (gm_x_row).

- [ ] **Step 1: Failing checks** in menu-check: `gm_tabs_game_rows` (an NES game: Continue Save Load "Quick Slot" "Player 2" Achievements Reset Quit), `gm_tabs_display_ds` (a DS game: Display Shader Screens "Swap Screens" Stylus), `gm_port_untabbed` (port: Continue Controls Quit), `gm_no_controls_row` (no GM_CONTROLS in any tab).
- [ ] **Step 2:** fails. **Step 3:** implement; the Controls tab shows Task 7's rows once it exists, until then an empty list.
- [ ] **Step 4:** `make check` passes; shots `--gamemenu 0 --gm-tab 0|1` in both themes look like the mocks' Game and Display tabs (content 860 wide, the tab row as MENU's).
- [ ] **Step 5: Commit** "In-game menu: Game, Display and Controls tabs, MENU's tab row".

### Task 7: The Controls tab

**Files:**
- Create: `src/ctltab.h`, `src/ctltab.c` (pure: the tab's rows from a model)
- Modify: `src/panel.h` (menu_style `side`, `side_w`), `src/main.c` (menu_panel sets rows and fill in by side_w and calls side; the Controls tab's keys, the picker, Reset's confirm, Hotkeys, the handheld and its motion; shot flags `--gm-tab 2`, `--gm-pick`)
- Modify: `src/controls.h`, `src/controls.c` (ctl_game gains `bool no_buttons`: the read-only pages without the system's buttons, for Hotkeys)
- Test: `tools/menu-check.c` (ctltab cases), `tools/controls-shots-check.sh` (the tab in both themes, still)

**Interfaces:**
- Consumes: rm_* (Task 3), rms_* (Task 4), td_pose/td_draw/td_keep (Task 2), tabs (Task 1, 6), SETSTICK (Task 5).
- Produces (ctltab.h):
  - row ids `typedef enum { CT_SCOPE, CT_OWN_NOTE, CT_STICK, CT_PAK, CT_FACE, CT_BUTTON, CT_WARN = CT_BUTTON + RM_BUTTONS, CT_RESET = CT_WARN + 16, CT_HOTKEYS, CT_MAX } ct_row;`
  - `typedef struct { const char *tag, *ext, *sys_label; bool scope_all, game_has_own, fixed_game; rm_table cur, def; bool stick_offered, stick_on; const char *pak; bool face_offered; rm_table letters; bool select_held_c, select_held_dpad; } ct_model;`
  - `int ct_rows(const ct_model *m, ct_row *ids, menu_row *out, unsigned *vcol, ct_bufs *b);` rows in spec §2's order; VCOL_EARNED on changed values and on Applies To when game_has_own; dead rows for "Held: C buttons"/"Held: D-pad", the own-buttons note and each warning
  - `unsigned ct_lit(const ct_model *m, ct_row id);` the td_key bits to light (spec §7)
- main.c behaviour: Left/Right and A per spec §2-§6; every map change sends `SETMAP\tmap=<rm_format>` with plat_resident_line, every stick change `SETSTICK\tmode=...` (no wait for STICK); Reset asks in confirm_rows' panel on Cancel, and in This Game also puts D-pad as Stick back to the system's default; A on a button opens the picker (heading "<button>: <system>", tick and "default"); Hotkeys opens game_controls_screen with `no_buttons`. The handheld: td_pose at the mocks' place, lit by ct_lit, motion per spec §8, its own td_keep.

- [ ] **Step 1: Failing checks:** `ct_rows_ps1_default` (Applies To, D-pad as Stick, A..SELECT, Reset Buttons dead, Hotkeys), `ct_rows_n64` (Pak present, SELECT dead "Held: C buttons"), `ct_rows_dc_face_stick_on` (Face Buttons present, SELECT dead "Held: D-pad"), `ct_scope_accent_when_own`, `ct_warn_under_cause` (B none → "× has no button now" right under B), `ct_lit_swap` (Y row after a swap with B lights Y and B), `ct_own_note_when_all` (scope all + game has own → the note row under Applies To).
- [ ] **Step 2:** fails. **Step 3:** implement ctltab.c, then main.c.
- [ ] **Step 4:** `make check` passes. Shots in both themes: `--gamemenu 0 --gm-tab 2` (PS1 Spyro), a swapped table, `--gm-pick`, the DC with the stick on, the N64; set beside the approved mocks (scratchpad boards), same layout; the fill never touches the handheld.
- [ ] **Step 5: Commit** "In-game menu: the Controls tab, its buttons, D-pad as Stick and the handheld".

### Task 8: The shelf: a game's own buttons on its details

**Files:**
- Modify: `src/game_menu.h`, `src/game_menu.c` (game_info gains `bool own_buttons`; gi_rows adds Buttons after Favorite, only when set), `src/main.c` (the details page's key: A on Buttons opens the Controls list standalone, heading the game's name, fixed on This Game, no game running)
- Test: `tools/menu-check.c` (`gi_buttons_row_only_when_own`)

- [ ] **Step 1:** failing check. **Step 2:** fails. **Step 3:** implement, reusing Task 7's screen with `fixed_game` and no resident.
- [ ] **Step 4:** `make check`; a `--info` shot of a game with `map.PS1.<file>` set shows "Buttons | This Game".
- [ ] **Step 5: Commit** "Game details: Buttons, for a game with its own".

### Task 9: Rename and Delete Game take the keys

**Files:**
- Modify: `src/gamern.c` (move `map.<TAG>.<file>` and `stick.<TAG>.<file>` beside `pak.`), `src/gamedel.c` (delete both)
- Test: the existing rename/delete checks (tools/gamern-check.c, tools/gamedel-check.c or whichever `make check` runs for them)

- [ ] **Step 1: Failing checks:** `rename_moves_map_and_stick` (file "Spyro the Dragon (USA).chd" renamed to "Spyro.chd": both keys move), `delete_drops_map_and_stick`.
- [ ] **Step 2:** fails. **Step 3:** implement. **Step 4:** `make check`. **Step 5: Commit** "Rename and Delete Game: a game's buttons and stick go with it".

### Task 10: Strings in every language

**Files:**
- Modify: `res/strings/gamemenu.txt` (tab names, Applies To, This Game, All %s, Reset Buttons and its two questions, Buttons, Held: C buttons, Held: D-pad), `res/strings/controls.txt` (D-pad as Stick, Face Buttons, By Letter, By Place, Mixed, Nothing, default, "%s has no button now", "This game keeps its own buttons.", "The game's")
- Generated: `src/i18n_ids.h`, `src/i18n_tables.c` (`make strings`)

Each entry has a `note =` saying where it is drawn and how long it may be, as the files do, and the eight languages. Tasks 6-8 used these ids; this task replaces any English placeholders with the real entries.

- [ ] **Step 1:** add the entries; `make strings`.
- [ ] **Step 2:** `make check-strings` passes; `python3 tools/i18n-shots.py` shows no cut string on the Controls tab in any language (the tab and the picker added to its shot list).
- [ ] **Step 3: Commit** "Strings: the in-game tabs and the Controls tab, in all eight languages".

### Task 11: Docs, README and the site

**Files:**
- Modify: `DESIGN.md` (11.28 rewritten; 11.3 the in-game tabs; 11.10 the shared handheld; a new 11.44 "Button remapping" with the swap, scope, warning and motion rules), `docs/guide.md` (how to change a button, per game or system, and the d-pad as the stick), `README.md` (the features list entry next to "Every control, in the game", and a gallery shot), `site/index.html` (a feature line and a row in the comparison table), `tools/readme-shots.py` (a `controls-tab` shot, light theme, Spyro backdrop with --art), `res/readme/controls-tab.png`
- Modify: `tools/controls-shots-check.sh` (the tab's shots)

- [ ] **Step 1:** write the docs in my first person; no em dashes.
- [ ] **Step 2:** `python3 tools/readme-shots.py --art --only controls-tab` and copy the light shot to res/readme/.
- [ ] **Step 3:** `make check` passes (check-design, check-building-doc, check-controls-shots included).
- [ ] **Step 4: Commit** "Docs: button remapping in DESIGN, the guide, README and the site".

### Task 12: Whole-branch check and the PR

- [ ] **Step 1:** rebase on origin/main if it moved; `make check` and `make -C diatom check` pass.
- [ ] **Step 2:** a QEMU run if the VM is free (tools/qemu-run.sh, docs/qemu.md): a NES game, MENU, the three tabs, a change on the Controls tab, Continue, the button does the new thing; then Quit and start again: kept.
- [ ] **Step 3:** open the PR "Button remapping: the in-game menu in tabs, and a Controls tab" against main, for 1.0.5, with the device checks it still needs (spec "Risks and checks on the device"): the PlayStation's analog mode, the menu's frame cost, SETMAP/SETSTICK on a GPU core, hotkeys after a SELECT remap.
