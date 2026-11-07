# Lexend and Card Art Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Phase 3 of the design-system roadmap: the UI speaks Lexend (Regular, and SemiBold for the title role), the Plain Jane system cards are drawn for each theme, Favorites and Muse lose their cyan and green, and the Wi-Fi File Transfer page wears the tokens.

**Architecture:** `res/fonts/menu.ttf` becomes Lexend Regular and a new `res/fonts/title.ttf` Lexend SemiBold, opened beside it; layout that assumed Josefin's metrics moves to cap-based centering. One generator, `tools/genplainjane.py`, draws every Plain Jane card for both themes from per-system marks (the icons, as RGBA layers) and `config/systems.cfg`'s colors; the launcher picks the art for the theme that is on and drops it when the theme turns, as it already does for generated cards.

**Tech Stack:** C (SDL2, SDL_ttf), Python 3 with Pillow for the generators, the existing `make check` suite.

**Spec:** `DESIGN.md` 3 (color), 4 (type), 6 (shape), 11.1 (cards), 12 (copy), 14 (open items); `docs/superpowers/plans/2026-11-04-design-system-roadmap.md` (phase 3).

## Global Constraints

- Code names roles, never values: no RGB literal outside `src/design.h`, `src/zypermark.h` and generated card art.
- Type: Lexend in two weights, Regular for everything and SemiBold for the title role; sizes title 60, label 55, menu 49, meta 37, card 71; Title Case; no italics, no letter spacing (DESIGN.md 4). The files stay `menu.ttf` (Regular) and `title.ttf` (SemiBold).
- Plain Jane system cards are drawn for each theme: a **surface** slab in Beard, the dark slab in Salt & Pepper, the system's color as a 6-unit band, the name in Lexend, Title Case, untracked, under the band; radius.card 22 (DESIGN.md 11.1, 6).
- A system's color as a mark on Beard is mixed 25% toward **text** (`ui_mix(acc, text, 0.25)`, `src/sysmark.c`).
- Favorites take the **favorite** role, Muse the **accent** (accent-ink as a mark on Beard).
- Fonts are OFL: `res/fonts/OFL.txt` and `THIRD-PARTY-LICENSES.md` name Lexend.

## Review Focus

1. A long system name ("Neo Geo Pocket Color", "Game Boy Advance") on a card and on the shelf's title line: it fits or wraps as designed, never clipped.
2. The keyboard's "unshift" and every action key in Lexend: the label and its glyph fit the key.
3. A theme turn with Plain Jane up: the cards are redrawn in the new theme on the next frame..
4. A card set without a Beard file for a system (a user's own set, or one card missing): the launcher falls back to the dark file, never to nothing.
5. The title role's file missing on an old card: the launcher falls back to `menu.ttf` for the title, never fails to start.

---

### Task 1: Lexend in the launcher

**Files:** `res/fonts/menu.ttf` (Lexend Regular), `res/fonts/title.ttf` (new, Lexend SemiBold), `res/fonts/OFL.txt`, `THIRD-PARTY-LICENSES.md`, `src/ui.c` (the title role and the button glyphs' 96 face open `title.ttf` from the folder `menu.ttf` is in; fall back to `menu.ttf`), `mk/payload.sh` (ship `title.ttf`, and the web copy), `res/web/app.css` (a 600 `@font-face`; its cyan becomes the tokens, light and dark by `prefers-color-scheme`), `docs/building.md`; layout that assumed Josefin: the shelf name at y 618 against the count at 690, half-descent centering (`src/main.c` 3725, 4278; `src/notice.c` 133; `src/keyboard.c` 231) moved to `ui_cap_y`, `menu_std_width`'s floor, the ui.h comments that name Josefin.

**Interfaces:** consumes `ui_cap_y`, `ui_cap_height`.

- [ ] **Step 1:** Swap the files and open the title role from `title.ttf` with the fallback; `make native`, `make check`.
- [ ] **Step 2:** Fix each metric-dependent layout by content; shots in both themes of the shelf (both card sets), menus, keyboard on each layer, notice, info, cheevos; compare with Task 2 of the themes plan's shots for regressions in spacing.
- [ ] **Step 3:** The Wi-Fi File Transfer page: its colors from the tokens (`tools/designdef.py` emits them, or the page states them with a comment that names the token), both themes.
- [ ] **Step 4:** Commit.

### Task 2: One generator for the Plain Jane cards

**Files:** Create `tools/genplainjane.py`, `res/cards/marks/<CARD>.png` (each system's icon as an RGBA layer, unblended once from today's PNG or drawn by the code that draws it today); write `res/cards/classic/<CARD>.png` (Salt & Pepper) and `res/cards/classic/light/<CARD>.png` (Beard); remove `tools/gensmallcards.py`, `gendscard.py`, `genmusecard.py`, `genportscard.py`, `genps1card.py`, `genfavcard.py` once their icons are marks; the Makefile's `cards` rule and its stale comment above `boot:`; a check (`make check-cards`) that every system in `config/systems.cfg` with a classic card has both files, 640x820, opaque but for corners (`cardgen_is_slab`).

**Interfaces:** the card layout I pick from the spike (name treatment A or B); colors from `tools/designdef.py` and `config/systems.cfg`.

- [ ] **Step 1:** Write the check; see it fail on today's tree (no light files).
- [ ] **Step 2:** Extract the marks; write the generator; generate all 25 cards in both themes.
- [ ] **Step 3:** Contact sheets of all cards in both themes; `make check`; commit.

### Task 3: The launcher picks Plain Jane art for the theme

**Files:** `src/main.c` (`sys_card_paths` takes the theme: a set's `light/` file in Beard, falling back to the set's file; the art texture tagged with its theme when it came from a per-theme file, so `theme_changed` drops it), `tools/flip-check.sh` (a Plain Jane pair).

- [ ] **Step 1:** Extend check-flip with a systems row on Plain Jane art; see it fail (the dark cards stay after a turn to Beard).
- [ ] **Step 2:** Implement; `make check`; shots of the systems row in both themes on both sets; commit.

### Task 4: DESIGN.md and the docs

**Files:** `DESIGN.md` 14 (remove what this phase finished: Lexend, the Plain Jane set, Favorites and Muse art, "unshift", the web page), 4 if Lexend's real width differs from what it says; `docs/guide.md` where it names the font or the cards.

- [ ] **Step 1:** Edit; `make design check-design check`; commit.

### Task 5: The port loading screen (My pick: "2 with backdrop")

The design lab (https://claude.ai/artifact/BvZ1okTQjvq4xjbQTAVqdN, source in the controller's scratchpad loading-lab/index.html, functions `opt2` and `opt3`) showed three layouts; I chose option 2's layout on option 3's backdrop.

**Files:** `src/main.c` (`port_loading_screen`), `src/ui.c`/`ui.h` (a balanced line break), `src/blur.c` (reused for the backdrop), a check for the line break.

- The backdrop: the cover scaled to fill the screen and blurred (drawn small, blurred with `src/blur.c`, stretched with linear filtering), under the theme's **ground** from 70% at the top to 90% at the foot, so **text** and **text-soft** keep 4.5:1 over any art. A game with no art blurs its generated card.
- The cover at equal area 368x512 units, centered at (272, 376), with the **focus** lift.
- A column at x 536, 456 wide, its block centered on y 312: a 48x6 band in the system's mark color, the system's name in **meta** **text-dim**, the game's name in **title** in up to three balanced lines 64 apart, and the line "Starting. This can take a minute." in **meta** **text-soft** (breaking after "Starting." when it does not fit).
- Zyper's loop under the column, its left edge on the column's: the frame's top-left at (504, 528) units, `loading.txt` x 315 y 330 in panel pixels.
- No "Loading..." anywhere (DESIGN.md 12).

- [ ] **Step 1:** A check for the balanced break (the widest line narrowest; one line when it fits; never more than the cap), seen failing first.
- [ ] **Step 2:** Implement; shots of three games (box art, no art, a long name) in both themes against the lab's option 2 and 3 screens; `make check`; commit.
