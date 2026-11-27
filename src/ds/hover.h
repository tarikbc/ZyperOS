/* SPDX-License-Identifier: GPL-3.0-only */
/* What an option list's cursor shows (DESIGN.md 11.3): the choice
 * under it, before A takes it. One piece for every list that shows one:
 *
 * - show(k): choice k on the screen, in the speaker or both, as it would be
 *   once chosen - the accent on every surface, the shelf in that layout,
 *   the click in that voice. In memory only: a show writes no setting and
 *   no file, so B, a close or POWER leave the card as it was.
 * - undo(kept): what is kept, back; `kept` the list's kept choice (-1:
 *   none on as a whole). Safe at any time, shown or not.
 * - draw(k, box): a picture of choice k in a box beside the list (the
 *   Display list's paused game, the Shader's demo). Drawn each frame; it
 *   changes nothing, so it needs no undo.
 *
 * A list runs it through ds_hover_run: show once each time the cursor
 * comes to another choice (never every frame: a show may relayout a shelf
 * or load fonts), undo when the cursor is back on the kept one, and undo
 * when the list closes, whichever way - before the choice A took is
 * applied, so the row's own Left and Right step it from the kept one as
 * they always did (src/menu.h's menu_choice) and its screen saves it.
 *
 * `rest_ms`: the cursor rests that long on a choice before it is shown
 * (Menu Music's Track: a track starts once the cursor stops on it, not on
 * each one it passes). 0: at once.
 *
 * `own_sound`: show plays the move's sound itself, after it has made the
 * choice heard (UI Sounds: the click in the voice under the cursor), so
 * the list's own move sound stays quiet; back on the kept one it is shown
 * too (its click heard), not undone.
 *
 * `tick`: each pass while the list is open, after any show (the panel's
 * table: a load a key's repeat held back is made once its gap is over).
 *
 * `end`: the list has closed, whichever way, shown or not, after any undo:
 * what draw made for it let go (Quick Slot's pictures, loaded once a list).
 *
 * `at`: told the cursor's choice on every pass, whatever is kept or shown,
 * and -1 once the list has closed, after any undo and before end (Rumble's
 * list: each level felt as the cursor reaches it, the kept one too; a
 * show would not be made back on the kept one).
 *
 * `keep_header`: the list keeps its page's header, the picture under the
 * page's heading, drawn as the show leaves it (On-Screen Stats' band: the
 * stats over the paused game where the choice puts them).
 *
 * Part of the design system (src/ds/): no app.h, no panel.h; `ctx` is the
 * caller's (the app). */
#ifndef ZYPEROS_DS_HOVER_H
#define ZYPEROS_DS_HOVER_H

#include <stdbool.h>

typedef struct ds_hover {
	void (*show)(void *ctx, int k);
	void (*undo)(void *ctx, int kept);
	void (*draw)(void *ctx, int k, int x, int y, int w, int h);
	void (*tick)(void *ctx);
	void (*end)(void *ctx);
	void (*at)(void *ctx, int k);
	unsigned rest_ms;
	bool own_sound;
	bool keep_header;
} ds_hover;

typedef struct ds_hover_run {
	const ds_hover *h;
	void *ctx;
	int kept;          /* the choice that is on; -1 none is on as a whole */
	int at;            /* the cursor's choice */
	int shown;         /* the choice shown; -1 none: what is kept is on */
	unsigned since;    /* when the cursor came to `at` (ms) */
} ds_hover_run;

/* A list opens with `kept` on (-1: none on as a whole) and the cursor on
 * `at`, at `now` (ms). Nothing is shown yet: the first ds_hover_at shows
 * `at` if it is not the kept one. */
void ds_hover_begin(ds_hover_run *r, const ds_hover *h, void *ctx, int kept, int at, unsigned now);

/* Each pass of the list, the cursor on `k`: shows k once it has rested
 * rest_ms on it (once per choice it comes to), or puts the kept one back
 * when k is the kept one; then tick. NULL h: nothing. */
void ds_hover_at(ds_hover_run *r, int k, unsigned now);

/* The list has closed, whichever way: undo if anything was shown, before
 * the choice taken is applied, then end. The run is spent (shown -1, no
 * hover). */
void ds_hover_end(ds_hover_run *r);

/* Whether the list's own move sound should stay quiet (own_sound) */
bool ds_hover_quiet(const ds_hover *h);

/* A list is showing a choice that is not kept (a show not yet undone):
 * whatever keeps what is on screen - the boot screen's words, the night's
 * hours, a zone Auto found - waits until it is undone, so a show is never
 * written down by something else */
bool ds_hover_showing(void);

#endif
