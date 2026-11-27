/* SPDX-License-Identifier: GPL-3.0-only */
/* A question with two answers (DESIGN.md 11.3, "A question with two
 * answers"): a panel (11.3) holding a heading, its rule, the question wrapped
 * to the panel and the two answers side by side under it, each a raised slab,
 * the focused one the **accent** fill. Part of the design system (src/ds/,
 * DESIGN.md "Where each part lives"): it lays out and draws; which question,
 * which words and what each answer does are the caller's (src/confirmq.h).
 *
 * Nothing is cut and nothing slides. The question wraps in **menu**; when its
 * lines do not fit the room over the answers it is set in **meta**, and only
 * past that does it scroll itself in its window, as a card's body does (the
 * last resort, which no question in the launcher needs: check-confirm). An
 * answer is in **menu** when each word fits its half, in **meta** (both)
 * when one does not, and the two stack only when one does not fit its half
 * even in **meta**. */
#ifndef ZYPEROS_DS_CONFIRM_H
#define ZYPEROS_DS_CONFIRM_H

#include <SDL.h>
#include <stdbool.h>

#include "../linebreak.h"   /* LB_LINE */
#include "menumotion.h"  /* mm_pose */
#include "ds/panel.h"    /* ds_pass */

/* An answer's marks */
enum {
	DS_CONFIRM_WARN = 1u << 0,   /* it loses something: its words in **warning** at rest */
	DS_CONFIRM_B    = 1u << 1,   /* B gives it: B's glyph before its word */
};

#define DS_CONFIRM_LINES 16

typedef struct {
	const char *heading;
	const char *text;         /* the question, whole: wrapped here */
	const char *answer[2];    /* left, right */
	unsigned mark[2];
	int width;                /* the content's width: menu_std_width */
	int sel;                  /* the answer under the cursor */
	float fill;               /* where the focus fill is, 0 on the left answer to 1 on the right */
	int pressed;              /* the answer pressing, -1 none */
	float press;              /* how far, 0 up to 1 down (eased by the caller) */
	unsigned phase;           /* ms since it opened: a question that scrolls (the last resort) */
} ds_confirm;

typedef struct {
	SDL_Rect panel;
	int content_x, content_w;
	char head[2][LB_LINE];
	int head_lines;           /* 1 or 2: a heading too wide is broken, never slid */
	int head_h;               /* the heading's band, the panel's top to its rule */
	bool meta;                /* the question in **meta** */
	char lines[DS_CONFIRM_LINES][LB_LINE];
	int nlines, step;         /* its lines and how far apart */
	int q_y;                  /* its top, from the panel's top */
	int window;               /* the room it has over the answers */
	bool scroll;              /* it scrolls itself in the room (the last resort) */
	bool rule;                /* a rule between it and the answers */
	bool side;                /* the answers side by side */
	bool small;               /* the answers in **meta** */
	SDL_Rect ans[2];          /* the answers' slabs, on the screen */
	bool whole;               /* every word shown whole, without a scroll */
} ds_confirm_lay;

/* Where everything goes; the panel centered on the screen `screen_w` wide,
 * never above `top`, at most `avail` tall */
void ds_confirm_layout(const ds_confirm *c, int screen_w, int screen_h, int top, int avail,
                       ds_confirm_lay *l);

/* Drawn at `pose`, as every panel is (src/ds/panel.h) */
void ds_confirm_draw(SDL_Renderer *r, const ds_confirm *c, int screen_w, int screen_h, int top,
                     int avail, mm_pose pose, const ds_pass *pass);

#endif
