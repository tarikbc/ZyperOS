/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_XFER_SCREEN_H
#define ZYPEROS_XFER_SCREEN_H

/* File Transfer (DESIGN.md 11.35, src/hare.h): its way in (Wi-Fi, the cable,
 * the hotspot) and its steps, the loading bar, the session kept open or
 * behind the shelf, the moving page, its calls into MP4 and the logs, and
 * System Update over the cable. The screens are src/xfer_screen.c. */
#include "app.h"
#include "panel.h"

/* File Transfer's steps (linksteps.h, DESIGN.md 11.35), while a step before
 * the browser stalls (xs_way_hint) and no one has come in yet: Cable,
 * Address and Browser over the cable, Wi-Fi and Browser over the air, each
 * a tick once done, the one waited on saying so and the ones after it
 * quiet, over the rule and the legend. The stalled step's hint takes the
 * others' room: its row alone and the hint under it, balanced onto at most
 * three lines, over the rule and the legend. Three rows and a three-line
 * hint do not fit the panel under a two-line heading, and a hint is about
 * one step. The screen and its shot (--hare-steps) build them here. */
#define STEP_ROWS 8

#define XS_STOP_L xs_words(0)

#define XS_STOP_W xs_words(1)

#define XS_STOP_LEGEND tiles_hints(T(S_KEYHINT_KEEP_GOING))

#define XS_STOP_M XS_MARKS[0]

#define XS_GAME_L xs_words(2)

#define XS_GAME_W xs_words(3)

#define XS_GAME_LEGEND tiles_hints(T(S_KEYHINT_NOT_NOW))

#define XS_GAME_M XS_MARKS[1]

/* What main.c uses of it; each is described where it is defined */
extern const int XS_MARKS[2][3];
const char *const *xs_words(int which);
void hare_head(char *out, size_t n, bool cable, const char *name, const char *addr,
               const char *pin);
void xs_way_set(bool cable, bool phone, const char *name, const char *addr, const char *pin);
int xs_way_rows(menu_row *out, const link_steps *s, bool cable, const char *legend);
bool xs_way_hint(int h);
void xs_way_draw(app *a, const menu_row *rows, int n);
float xs_load_target(const link_steps *s, unsigned now);
void xs_load_draw(app *a, const menu_row *rows, int n, float k);
void hot_head(char *out, size_t n, int room, const char *ssid, const char *pass,
              const char *pin);
int xs_step_rows(app *a, menu_row *out, char lines[3][LB_LINE], char *hint, size_t hn,
                 const link_steps *s, int h, bool cable, const char *legend);
void hot_panel(app *a, const char *heading, const menu_row *rows, int n,
               mm_pose pose);
void hot_preview(app *a, const char *ssid, const char *pass,
                 const char *phones, const char *now);
void xs_move_set(const xs_flow flow[2], long long got, long long of);
int xs_page_rows(menu_row *out, const char *who, const char *ssh, const char *now, str_id word);
void move_page(app *a, const char *heading, const menu_row *rows, int n, mm_pose pose);
void xs_page_show(app *a, const char *heading, const menu_row *rows, int n);
/* The screen's views (xfer_screen_way): the way in, the cable's loading bar,
 * a stalled step's steps and hint, the news (Flash Drive's page), and the
 * hotspot's way in, which is hot_show's */
enum { XS_VIEW_IN, XS_VIEW_LOAD, XS_VIEW_STEPS, XS_VIEW_NEWS, XS_VIEW_HOT };
/* One frame of the panel for `view` (not XS_VIEW_HOT), for the screen and
 * the shot harness alike: the rows are the view's own (xs_way_rows,
 * xs_step_rows, xs_page_rows), `head` the heading, `load_k` the bar's fill */
void xs_view_draw(app *a, int view, const char *head, const menu_row *rows, int n,
                  float load_k);
void hare_preview(app *a, const char *name, const char *addr, const char *pin,
                  const char *who, const char *ssh, const char *now, str_id word);
void up_cable_tidy(void);
bool xs_moving(void);
void after_start_step(app *a, bool drawn);
void xs_tick(app *a);
bool xs_shelf_due(void);
void xs_mark(status_marks *m);
int xs_finish_rows(menu_row *rows, char *line, size_t ln, const char *file);

#endif
