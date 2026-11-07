/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_KEYBOARD_H
#define ZYPEROS_KEYBOARD_H

#include <SDL.h>
#include <stdbool.h>

#include "platform.h"

/* On-screen text entry.
 *
 * General on purpose. WiFi needs a network password, and the moment WiFi
 * works RetroAchievements needs a username and a password too, so this takes
 * a title and a buffer and returns a string rather than knowing anything
 * about what it is being typed into. A keyboard welded into the WiFi flow
 * gets rewritten the first time something else needs to type.
 *
 * Modal and blocking, in the same shape as the menus: poll, act, draw,
 * present, delay. The caller supplies a panel callback because this module
 * knows nothing about the shelf, and something has to be drawn behind a panel
 * that does not cover the screen.
 */

typedef enum {
	KB_CANCEL,   /* MENU, or B held on an empty field */
	KB_ACCEPT,   /* START */
	KB_POWER     /* the power button, handed back for the caller to act on */
} kb_result;

/* The owner's existing panel stack, without exposing app internals here.
 * OPEN/LEAVE/CUT/FINISH only update that stack. FRAME draws the backdrop
 * and any departing parent, then returns the keyboard's pose. During a
 * leave, moving is false when the keyboard is gone. A press settles an
 * arrival or cuts a leave. CUT handles power/quit without another frame.
 * FINISH must release all references to the keyboard before return; the
 * keyboard itself owns and destroys every texture containing typed text. */
typedef enum { KB_PANEL_OPEN, KB_PANEL_FRAME, KB_PANEL_LEAVE,
               KB_PANEL_CUT, KB_PANEL_FINISH } kb_stage;
typedef struct { float scale, alpha; bool moving; } kb_pose;
typedef kb_pose (*kb_panel)(void *ctx, kb_stage stage, bool pressed);

/* Asked every frame: has the device been left alone long enough to switch
 * itself off? Returning true is reported as KB_POWER, because that is what it
 * means and every caller already knows what to do with it. A callback rather
 * than a timeout value, for the same reason as the backdrop: this module is
 * general and holds no policy. May be NULL. */
typedef bool (*kb_idle)(void *ctx);

/* `buf` is both the seed and the result: pass an empty string for a fresh
 * entry, or existing text to edit. `cap` is the size of buf including the
 * terminator.
 *
 * It takes no colors. Every one is a role of the theme that is on, asked for
 * as each frame is drawn (DESIGN.md 11.5): a color handed in when the
 * keyboard opened would be the theme of that minute, and Auto turns Beard
 * into Salt & Pepper at 19:00 whatever is open. The backdrop is where the
 * caller ticks the theme.
 *
 * The text is always shown in clear. A password typed a character at a time
 * on a d-pad is going to be typed wrong, and a masked field turns that into a
 * failed association with no way to tell a typo from a weak signal or a
 * supplicant that never came up. Hiding it would protect against a shoulder,
 * at the cost of the one thing the user needs to see. */
kb_result kb_prompt(SDL_Renderer *r, in_state *in, const char *title,
                    char *buf, int cap, kb_panel panel, kb_idle idle,
                    void *ctx);

/* One frame, no loop, for the --keyboard shot harness. The panel is dense
 * enough that laying it out against a screenshot beats laying it out against
 * a device, and the same argument already justifies --menu and --slots.
 * `layer` is 0 lower, 1 upper, 2 symbols. (`row`, `col`) is the selected
 * key: rows 0-3 the grid, row 4 the action row with `col` its key
 * (src/kbnav.h). */
void kb_preview(SDL_Renderer *r, const char *title, const char *text,
                int layer, int row, int col);

/* The same frame with the selected key pressed at time zero, sampled at
 * `elapsed_ms`. The entire key, including its label/glyph, pops. This is an
 * explicit motion preview; kb_preview itself always stays at rest. */
void kb_preview_press(SDL_Renderer *r, const char *title, const char *text,
                      int layer, int row, int col, unsigned elapsed_ms);

/* Explicit panel-motion shot. No typed text or texture survives the call. */
void kb_preview_panel(SDL_Renderer *r, const char *title, const char *text,
                      int layer, int row, int col, kb_pose pose);

#endif
