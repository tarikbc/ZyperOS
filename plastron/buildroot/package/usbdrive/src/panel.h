/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The Flash Drive page on the panel, held by usbdrive itself so its numbers
 * can change (atlas.h): one dumb buffer scanned out, as splash --still
 * holds a picture, and written in place where a slot changed. The display
 * is let go of (drmDropMaster) at once, so the next splash --still or
 * ZyperOS takes it as usual; the buffer stays on glass until then.
 */
#ifndef USBDRIVE_PANEL_H
#define USBDRIVE_PANEL_H

#include <stdbool.h>
#include <stdint.h>

/* The landscape picture `w` x `h` up on the panel. False when there is no
 * display to take, or it is neither the picture's size nor turned to it. */
bool panel_open(const uint32_t *page, int w, int h);

/* The rect of `page` (landscape) onto the panel again */
void panel_update(const uint32_t *page, int x, int y, int rw, int rh);

/* A small landscape picture `w` x `h` onto the panel at (x, y) */
void panel_blit(const uint32_t *px, int w, int h, int x, int y);

/* Let go of the buffer (after another program put its picture up) */
void panel_close(void);

#endif
