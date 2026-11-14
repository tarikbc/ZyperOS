/* SPDX-License-Identifier: GPL-3.0-only */
/* Marks that stand for a state: a badge (a disc with a mark in it), a switch
 * as a mark (lit or a quiet ring), and the progress bar. Part of the design
 * system (src/ds/, DESIGN.md "Where each part lives"). Colors are the theme's
 * roles, asked for as they are drawn. */
#ifndef ZYPEROS_DS_MARK_H
#define ZYPEROS_DS_MARK_H

#include <SDL.h>
#include <stdbool.h>

#include "ds/glyph.h"

/* A disc `d` across centred on (cx, cy), in a DS_BADGE_RING **edge** ring,
 * with `g` in it at 64% of `d`: `lime`, the **accent** with **on-accent** on
 * it (on, a video); else **surface** with **accent-ink** (a source). `al`
 * its opacity. The Scraper's covers and marks (DESIGN.md 11.19). */
void ds_badge(SDL_Renderer *r, int cx, int cy, int d, ui_glyph g, bool lime, Uint8 al);

/* A switch as a mark: on, the lime badge; off, a quiet ring of **edge** on
 * **raised** with `g` at 60% in **text-dim** */
void ds_switch(SDL_Renderer *r, int cx, int cy, int d, ui_glyph g, bool on);

/* The progress bar, UI_BAR_H (DS_BAR) high, `w` long at (x, y): **track**,
 * and `fill` of it from the left in **accent-ink** where fill > 0. The
 * caller says how much is filled, so each keeps its own rounding. */
void ds_bar(SDL_Renderer *r, int x, int y, int w, int fill);

#endif
