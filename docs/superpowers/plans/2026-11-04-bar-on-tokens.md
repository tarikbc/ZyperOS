# The bar on the design tokens: implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The volume and brightness bar (PR #7, tarikbc/diatom#1) draws from `src/design.h` in either theme, moves on DESIGN.md's motion, and every process that draws it reads the theme from one file.

**Architecture:** `src/osdbar.c` takes a theme index and reads its colors and timings from `src/design.h`. A new `src/themefile.c` reads and writes `/tmp/zyperos-theme`; the launcher writes it, Diatom and portkeys read it each time the bar comes up. Until the themes PR (phase 2) adds MENU > Appearance, the launcher writes `dark`, so the bar matches the launcher's current dark look.

**Tech Stack:** C11, no SDL in osdbar/themefile; SDL2 in platform.c; Diatom's GLES port.

**Spec:** `DESIGN.md` 11.6 (the bar), 8 (motion), 3 (color).

## Global Constraints

- Volume marks and segments are **accent-ink**, brightness **light**; body **surface** at 93%, frame **edge**, unfilled **track** (DESIGN.md 11.6).
- In: `DS_MS_QUICK` (120 ms) on `ds_out`, sliding down `OSDBAR_RISE` (14 units); held `DS_MS_HOLD_BAR` (1500) after the last press; out `DS_MS_FADE_OUT` (200) linear.
- Shadow: `DS_THEME[t].bar_shadow` (ink 40% dark, 18% light), 4 down, 8 blur.
- Theme file: `/tmp/zyperos-theme`, one line, `light` or `dark`; missing or unreadable means `light` (the default theme).
- `src/osdbar.c`, `src/osdbar.h`, `src/design.h`, `src/themefile.c`, `src/themefile.h` stay plain C with no SDL, GPL-3.0-or-later OR MPL-2.0, and are copied whole into Diatom's `port/`.

## Review Focus

1. The theme file is absent after a cold boot until the launcher writes it: Diatom and portkeys must draw the light bar, not fail.
2. The file is rewritten while Diatom reads it: a reader must never see a half-written file (write to a temporary name and rename).
3. A press while the bar fades out: it comes back to full where it is, with no second slide (already tested by `check-osdbar`; keep it).
4. portkeys over a port started before the launcher ever wrote the file (first boot straight into a port): light bar.
5. Diatom's copy drifting from ZyperOS's: the Diatom PR states the ZyperOS commit it was copied from.

---

### Task 1: Rebase the bar onto the design system

**Files:** branch `claude/osd-pill` onto `claude/design-system`; conflict expected in `Makefile` (CHECKS and .PHONY lines).

- [ ] **Step 1:** `git rebase claude/design-system` on `claude/osd-pill`; resolve `Makefile` by keeping both `check-osdbar` and `check-design`.
- [ ] **Step 2:** Run `make check`. Expected: `ok: every check passed`.
- [ ] **Step 3:** `git push --force-with-lease origin claude/osd-pill`, and note in PR #7 that it now sits on #8.

### Task 2: osdbar draws from the tokens, in either theme

**Files:**
- Modify: `src/osdbar.h`, `src/osdbar.c`, `tools/osdbar-check.c`, `src/platform.c` (caller), `tools/portkeys.c` (caller)

**Interfaces:**
- Produces: `void osdbar_render(uint32_t *px, int w, int h, int stride, float scale, int theme, int kind, int level, int max, float alpha, bool premul);` with `theme` `DS_LIGHT` or `DS_DARK`. `OSDBAR_IN_MS`, `OSDBAR_HOLD_MS`, `OSDBAR_OUT_MS` become `DS_MS_QUICK`, `DS_MS_HOLD_BAR`, `DS_MS_FADE_OUT`; the envelope's arrival uses `ds_out`.

- [ ] **Step 1: Write the failing tests** in `tools/osdbar-check.c`: `segments()` takes a theme and checks a lit segment equals `DS_THEME[t].accent_ink` (volume) or `.light` (brightness) and an unlit one `.track`, for both themes at scales 1 and 0.625; `ck(osdbar_envelope(OSDBAR_IN_MS/2, OSDBAR_IN_MS/2, &a, &r) && fabsf(a - ds_out(0.5f)) < 0.01f, "arrives on the quintic ease-out")`; the fade-out half-way test uses `DS_MS_FADE_OUT`.
- [ ] **Step 2:** `make check-osdbar`. Expected: FAIL (wrong arity, cyan segments).
- [ ] **Step 3:** Implement: remove the local PANEL/EDGE/TRACK/VOLUME/BRIGHT arrays and read `DS_THEME[theme]`; shadow opacity from `bar_shadow`; envelope on `ds_out`.
- [ ] **Step 4:** `make check-osdbar && make native`. Expected: `osdbar: ok`, clean build.
- [ ] **Step 5:** Commit.

### Task 3: The theme file

**Files:**
- Create: `src/themefile.h`, `src/themefile.c`, `tools/themefile-check.c`
- Modify: `Makefile` (check-themefile in CHECKS), `src/platform.c`, `tools/portkeys.c`, `mk/cross.mk` (portkeys sources)

**Interfaces:**
- Produces: `#define THEME_FILE "/tmp/zyperos-theme"`; `int themefile_read(const char *path);` returning `DS_LIGHT` or `DS_DARK`, `DS_LIGHT` when missing or unrecognised; `bool themefile_write(const char *path, int theme);` writing `path.tmp` then `rename`.

- [ ] **Step 1: Write the failing test** `tools/themefile-check.c`: missing file reads `DS_LIGHT`; after `themefile_write(p, DS_DARK)` it reads `DS_DARK`; a file holding `"dark\n"`, `"dark"`, `" dark \n"` reads `DS_DARK`; `"purple"` and an empty file read `DS_LIGHT`; no `p.tmp` left behind after a write.
- [ ] **Step 2:** `make check-themefile`. Expected: FAIL (no file).
- [ ] **Step 3:** Implement. The launcher writes `DS_DARK` at startup in `plat_settings_init`'s caller until phase 2; `plat_draw_osd` passes `DS_DARK`. portkeys calls `themefile_read(THEME_FILE)` in `bar_show` and redraws if it changed.
- [ ] **Step 4:** `make check`; compile portkeys in the arm64 container (`gcc ... tools/portkeys.c src/audioout.c src/osdbar.c src/themefile.c -lsqlite3 -lm`). Expected: both clean.
- [ ] **Step 5:** Commit; push PR #7; host shot `--osd volume 13` attached to the PR.

### Task 4: Diatom's copy

**Files (tarikbc/diatom, branch `zyper-osdbar`):**
- Modify: `port/osdbar.c`, `port/osdbar.h` (copied from ZyperOS), `port/pixel2.c`, `Makefile`
- Create: `port/design.h`, `port/themefile.c`, `port/themefile.h` (copied)

- [ ] **Step 1:** Copy the five files with the vendored header naming the ZyperOS commit.
- [ ] **Step 2:** `draw_level_bar` reads `themefile_read(THEME_FILE)` when the bar comes up (in `osd_show`, under `g_mx`) and keeps the theme in `g_osd_theme`; the texture is redrawn when the theme changes.
- [ ] **Step 3:** Syntax-check `port/pixel2.c` in the arm64 container and run `make check` on the desktop port. Expected: clean, all pass.
- [ ] **Step 4:** Commit and push diatom#1.
