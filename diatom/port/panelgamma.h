/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* Vendored from ZyperOS src/panelgamma.h, which is where it is changed: the
 * launcher and this port empty the display's gamma table with the same code.
 * Copy it back here whole, below this note, when it changes there: make check
 * fails while the two differ (tools/check-copies.py). */
/* The display controller's gamma table, emptied, so the panel shows each
 * frame's own colors.
 *
 * The GKD Pixel 2's display controller (the PX30's VOP) has a 256-entry
 * table per channel between the frame and the panel, and plastron's splash
 * filled it at every boot up to ZyperOS 1.0.4: red and green as in^1.5, blue
 * as it was, a correction set by eye against a TrimUI Brick. It darkened
 * every game and the shelf alike, and took the warmth out of them: a mid
 * grey (128,128,128) reached the panel as (91,91,134), a warm sand
 * (216,184,120) as (199,156,133). The stock firmware fills no table, so the
 * same game looked brighter and warmer there, which is how a player found
 * it (SNES and PS1, 2026-11-12). The splash no longer fills it; this empties
 * the table an older system image's splash filled, since System Update over
 * Wi-Fi brings a new ZyperOS folder to a card whose system image may be
 * older. The launcher calls it when it takes the display from the splash,
 * and Diatom when it takes the display (it can come first, with a game
 * resumed at boot).
 *
 * The kernel's own ioctls, with the numbers and structures written out here
 * rather than taken from <drm/drm.h> or libdrm: the launcher links no libdrm,
 * the structures are the kernel's fixed ABI, and the check
 * (tools/panelgamma-check.c) builds on a computer without Linux's headers.
 *
 * Diatom carries a copy, diatom/port/panelgamma.c and .h, checked against
 * these by diatom/tools/check-copies.py. */
#ifndef PANELGAMMA_H
#define PANELGAMMA_H

/* ioctl(2) as these call it; a check puts a stand-in display here. */
typedef int (*panelgamma_ioctl_fn)(int fd, unsigned long request, void *arg);
extern panelgamma_ioctl_fn panelgamma_ioctl;

/* Empty every CRTC's GAMMA_LUT that holds a table. Needs DRM master on fd.
 * Returns how many it emptied: 0 when none held one, or the kernel has no
 * GAMMA_LUT (QEMU's display, a kernel without plastron's patch 7); -1 when
 * asking or emptying failed. A CRTC whose table is already empty is left
 * alone: emptying one makes the controller wait a refresh. */
int panelgamma_clear(int fd);

#endif
