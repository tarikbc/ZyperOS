/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_ABOUT_SCREEN_H
#define ZYPEROS_ABOUT_SCREEN_H

/* MENU > About (DESIGN.md 11.10): its pages - the system, its details, the
 * name's story and the thanks - and MENU > Controls, the handheld's own
 * buttons. The screens are src/about_screen.c; About's rows are
 * src/about.c's. */
#include "about.h"
#include "app.h"
#include "devmode.h"
#include "panel.h"

/* Four pages: the system, its details, the name, and the thanks (
 * 2026-11-06: "another page ... telling the story of the name that it's a
 * tribute to my dog that passed away, and a thank you page to the Open
 * source repos we got stuff"). The story is the README's own words, laid
 * out as a synopsis is (syn_layout): taller than the panel, it walks. The
 * thanks were three pages until 2026-11-16 ("so it scrolls instead of being
 * multiple pages, like the one that explains the name"): one page now,
 * walked by hand as the story is. */
#define ABOUT_PAGES 4

/* the credits, the handhelds' heading and names, and the footer */
#define ABOUT_THANKS_ROWS 32

typedef struct {
	char ver[48], addr[80], batt[32], up[48];
	char restart[96];        /* the Last Restart row's value: "K1W · 0.6 s" */
	char kernel[64];         /* "the Pixel 2 kernel": the handheld's own */
	menu_row system[6], details[ABOUT_DETAIL_ROWS];
	int nsystem;             /* 4; one more with Last Restart (lastboot.h), one more
	                          * with the Developer Mode row (devmode.h), always last */
	devmode_hold hold;       /* L2 + R2, five seconds: Developer Mode's unlock */
	char story_lines[SYN_WRAP_LINES][CHV_WRAP_COLS];
	menu_row story[SYN_ROWS], thanks[ABOUT_THANKS_ROWS];
	int nstory, story_loop, nthanks;
	int story_scroll;        /* pixels the story is scrolled by, by hand */
	int thanks_scroll;       /* and the thanks */
	SDL_Texture *photo;
	about_info info;
	about_values values;
	bool primed;
	unsigned refreshed;
	int width;
} about_view;

/* What main.c uses of it; each is described where it is defined */
void about_pages(app *a, about_view *v);
int about_width(app *a, const about_view *v);
void about_refresh(about_view *v, unsigned now);
void about_draw(app *a, const about_view *v, int page);

#endif
