/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_UI_H
#define ZYPEROS_UI_H

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdbool.h>

#include "theme.h"   /* theme(): every color below is a role of the one on */
#include "linebreak.h" /* LB_LINE: a line of a balanced break */
#include "i18n.h"      /* lang: the faces follow the language */

/* The look: the theme's ground, one accent per system, and nothing on screen
 * that is not either a card, the name of what is under the cursor, or a rail
 * saying where you are in the list.
 *
 * Colors are ROLES of the theme that is on (DESIGN.md 3.2), asked for when
 * they are drawn and never kept: Auto turns Beard into Salt & Pepper at 19:00
 * under whatever is on screen, and a color copied into a static at start
 * would stay in the theme of the hour the launcher started in. So none of
 * these is a constant, and none can be used to initialize a static. */

/* A role as SDL wants it, at opacity `a`. */
static inline SDL_Color ui_col(ds_rgb c, Uint8 a)
{
	return (SDL_Color){ c.r, c.g, c.b, a };
}

/* A role as 0xRRGGBB, the form ui_lift and a menu's value colors take a
 * system's color in, for a role that stands where one would. */
static inline unsigned ui_rgb(ds_rgb c)
{
	return ((unsigned)c.r << 16) | ((unsigned)c.g << 8) | c.b;
}

/* What clears the screen: the theme's ground. */
#define UI_BG_R (theme()->ground.r)
#define UI_BG_G (theme()->ground.g)
#define UI_BG_B (theme()->ground.b)

/* Three steps of text, brightest first. A list is read at all of them at
 * once, so the gaps have to be wide enough to rank the rows and narrow enough
 * that the quietest step is still text rather than texture. SOFT is where an
 * ordinary unselected row sits: selection is said by the highlight behind it,
 * not by dimming everything else down to it. Each step is the theme's, so
 * "brightest" is the most contrast with the ground: charcoal on Beard. */
#define UI_TEXT      ui_col(theme()->text, 255)
#define UI_TEXT_SOFT ui_col(theme()->text_soft, 255)
#define UI_TEXT_DIM  ui_col(theme()->text_dim, 255)

/* One thickness for every horizontal indicator: the position rail across the
 * bottom and the Now Playing track are the same bar in two places, so they
 * are the same weight. (The settings line across the top was the third, until
 * it became src/osdbar.c's pill.) */
#define UI_BAR_H 6

bool ui_init(SDL_Renderer *r, const char *font_path);
void ui_quit(void);

/* TTF_OpenFont and TTF_CloseFont, one thread at a time. SDL_ttf opens every
 * face in one FreeType library, and FreeType's rule for a library shared by
 * threads is a lock around making and destroying faces (its FT_Library and
 * FT_Face notes); what is done with a face once it is open needs no lock, so
 * long as one thread uses it at a time. The art workers open faces of their
 * own to draw the generated card (ui_card_paint), so every face the launcher
 * opens or closes, on any thread, goes through these two. */
TTF_Font *ui_face_open(const char *path, int pt);
void ui_face_close(TTF_Font *f);

/* After a present in a loop that draws every frame: the rest before the next
 * one. Nothing when the renderer waits for the panel (vsync granted): SDL's
 * present already waits for the last frame's flip, so the panel paces the
 * loop, and a sleep on top only came out of the next frame's time. With 8 ms
 * of a 16.7 ms frame gone, a frame that took more than about 9 ms to draw
 * missed its blank and the menus ran at 30 on the Pixel 2 (2026-11-05).
 * Without vsync (a window that declined it, a software renderer) it sleeps
 * `ms`, which is what keeps such a loop off a whole core. */
void ui_frame_rest(Uint32 ms);

/* The type scale. One base size and a multiplier per role -- so retuning a
 * role is one number here instead of a size hunted down at every call site,
 * and the whole scale can move together, which on a panel this dense is
 * usually what is wanted.
 *
 * UI_F_CARD is drawn into the 512px-wide generated card rather than onto the
 * screen, so it is sized for the card and not for the panel. */
typedef enum {
	UI_F_TITLE,   /* the name of the thing under the cursor */
	UI_F_MENU,    /* menu rows */
	UI_F_LABEL,   /* headings, slot names */
	UI_F_META,    /* counts, timestamps -- the quiet line */
	UI_F_CARD,
	UI_F_COUNT
} ui_font_role;

/* The font for a role. One size: the Text Size setting is gone, and src/ui.c
 * says why. In the language that is on: Lexend, or in 简体中文 the cut of
 * Noto Sans SC (DESIGN.md 4). */
TTF_Font *ui_font(ui_font_role role);
/* The faces ui_font answers with from now: `l`'s (src/i18n.h). Called with
 * i18n_set. The Chinese set opens the first time and is kept. */
void ui_fonts_for(lang l);
/* Whether ui_font answers with a Chinese or Japanese set, and which set:
 * 0 Lexend's, 1 Chinese's, 2 Japanese's */
bool ui_fonts_cjk(void);
int ui_font_set(void);
/* The face's file of the set that is on (Chinese's while Lexend's is),
 * beside menu.ttf: the title's or the rest's */
const char *ui_cjk_font_path(bool title, char *out, size_t n);
/* `role`'s face for `l`'s words, whatever language is on: each language's
 * own name in the picker and in MENU > Language's panel */
TTF_Font *ui_font_lang(ui_font_role role, lang l);
/* A role's line: its size, an em (DESIGN.md 4's 60, 55, 49, 37), the unit
 * menu rows and stacked lines are laid out in. Not the font's line skip,
 * which is its ascent and descent: Lexend's ascent is a whole em, a quarter
 * of it room for accents over the capitals, so its skip is 1.25 em, where the
 * face before it had three quarters and a skip of one em. The layouts were
 * drawn on the em, and the mock kit's menu row (77 units for **menu**) is
 * near 1.5 em, not 1.5 skips (93). A rendered line's texture is still the
 * font's height, taller than this; a line is placed by its capitals, below. */
int ui_font_line(ui_font_role role);

/* Draw text with its top-left at (x,y). anchor: -1 left, 0 center, 1 right,
 * applied to x. Returns the drawn width. Rendering is cached per (font,
 * string), so redrawing the same title every frame costs one blit. The top
 * is the texture's, ascent and all: a caller places a line with ui_line_y or
 * ui_cap_y rather than by this top, or the face's ascent decides where its
 * letters land. */
int ui_text(SDL_Renderer *r, TTF_Font *f, const char *s, int x, int y,
            int anchor, SDL_Color col);

/* The same color at a fraction of its opacity, for text that fades in or out.
 * Clamped, so a caller doing its own easing cannot overshoot into a wrapped
 * alpha byte. */
static inline SDL_Color ui_fade(SDL_Color c, float k)
{
	if (k < 0.0f) k = 0.0f;
	if (k > 1.0f) k = 1.0f;
	c.a = (Uint8)(c.a * k + 0.5f);
	return c;
}
int ui_text_width(TTF_Font *f, const char *s);
/* The rendered line, as ui_text draws it: TTF's blended surface, with the
 * word MENU drawn as the Pixel 2's round three-dot key (see ui.c). The caller
 * frees it. */
SDL_Surface *ui_text_surface(TTF_Font *f, const char *s, SDL_Color col);

/* Centering a line on its ink, DESIGN.md 4, by its capitals: ui_cap_y is the
 * y to draw a line of `f` at (ui_text's top) so that the middle of its
 * capitals is `mid`, and ui_cap_height is how tall they stand above the
 * baseline ('H'). By the capitals and not by half the descent, because the
 * box above them is the font's to choose - Lexend's ascent is a whole em,
 * where the Josefin Sans before it had three quarters - and the capitals are
 * where the eye puts a line's middle. One rule, so a button glyph's letter
 * and the label beside it share a middle. */
int ui_cap_height(TTF_Font *f);
int ui_cap_y(TTF_Font *f, int mid);

/* The y to draw a line of `role` at (ui_text's top) when the layout gives the
 * top of its line, ui_font_line tall, rather than a middle to center it on: a
 * title under the screen's top edge, a count over the rail, the lines of Now
 * Playing one under another. Its capitals are centered two fifths of the way
 * down the line, which with Lexend's (0.7 em) puts them a twentieth of an em
 * under the top and the baseline three quarters down, the descenders in the
 * quarter under it - where the face before had them, so every such layout
 * kept its numbers. */
int ui_line_y(ui_font_role role, int top);

/* ui_text with every digit the same width, the way tabular figures are: for a
 * number that changes while it is on screen, so a clock ticking from 1:19 to
 * 1:20 does not shift what is beside it. Lexend's digits are proportional, as
 * Josefin Sans's before it were, and SDL_ttf turns on no OpenType feature to
 * ask for others - measured 2026-11-04, a 1 is 500 units wide and a 4 is 620
 * - so each digit is drawn centered in a cell as wide as the widest one, and
 * everything else in the string as it always is. */
int ui_text_tabular(SDL_Renderer *r, TTF_Font *f, const char *s, int x, int y,
                    int anchor, SDL_Color col);
int ui_text_tabular_width(TTF_Font *f, const char *s);

/* The ping-pong offset a marquee is at, in pixels, for `phase` ms into it.
 * Exposed so a panel that scrolls itself vertically keeps the same timing as
 * a title that scrolls sideways. */
int ui_pingpong(int over, unsigned phase);

/* How many ms until ui_pingpong's answer for `over` next changes, from `phase`:
 * 0 while it is moving, the rest of the hold while it is standing still.
 *
 * So a caller that only draws when something changes can sleep through the
 * holds. A marquee ping-pongs for as long as its title is focused, and without
 * this it would keep the whole screen redrawing at the refresh rate the entire
 * time, through 1.4s of stillness at one end and 0.9s at the other. */
unsigned ui_pingpong_wait(int over, unsigned phase);

/* How many ms into a marquee for `over` its far end has been shown and held,
 * the whole text read once: for something on screen for a while that is to
 * stay until its words have been (the song pop-up, DESIGN.md 11.22). 0 when
 * it fits. */
unsigned ui_pingpong_reach(int over);

/* Copy `src` into `dst`, shortened with an ellipsis until it fits `maxw`
 * pixels in `f`. Bytes are stepped back one at a time and then walked off any
 * UTF-8 continuation, so a multi-byte character is never cut in half.
 *
 * One implementation because there is one rule. This lived inside notice.c,
 * where a name too long for an overlay had to be trimmed; the box art screen
 * needs exactly the same thing for a ROM name too long for a panel, and a
 * second copy would be a second set of decisions about where the dots go.
 * The rule itself is src/linebreak.c's lb_fit, which has no SDL, so the
 * balanced break below cuts a word too wide for its box the same way and
 * tools/linebreak-check.c holds both. */
void ui_fit_text(TTF_Font *f, const char *src, char *dst, size_t dstn,
                 int maxw);

/* `text` in `f`, broken into at most `cap` lines no wider than `maxw`
 * pixels, into `lines`; how many. One line when it fits, else the fewest
 * lines it fits in and of those the break whose widest line is narrowest, a
 * word too wide for the box cut as ui_fit_text cuts one, and a text that
 * fits in no `cap` lines cut at the end of the last: src/linebreak.h's
 * lb_balance, measured in `f`. 0 for no font. */
int ui_balance_lines(TTF_Font *f, const char *text, int maxw, int cap,
                     char lines[][LB_LINE]);

/* Forget the widths, fits and balances remembered on the main thread (ui.c
 * keeps the last 64, as each is a pure function of the face, the text and the
 * box). Called wherever a face the main thread measures with is closed: a new
 * face opened at the same address must not be answered for the old one. */
void ui_text_memo_clear(void);

/* A new frame starts: the rects and lines the host's overflow check kept of
 * the last are forgotten (src/ui.c, "the box a string must stay inside") */
void ui_fit_frame(void);
/* A string drawn some way that is not ui_text (a notice's, rendered for
 * Diatom to show): said as drawn to the host's check, which holds it to its
 * box where it is cut (ui_fit_text) */
void ui_fit_drawn(const char *s);
/* A menu row's value `s`, `w` wide, in `room` of a row `row_w` wide: said
 * on a host when it fits by less than 2% of the row (see ui.c) */
void ui_fit_tight(const char *s, int w, int room, int row_w);
#ifdef I18N_PSEUDO
/* A cleared screen is a new frame for it too */
#define SDL_RenderClear(r) (ui_fit_frame(), SDL_RenderClear(r))
#endif

/* Draw `s` left-aligned at (x,y), clipped to `w` pixels, sliding it back and
 * forth when it does not fit.
 *
 * Truncation destroys information permanently; this only delays it. `phase` is
 * milliseconds since the subject last changed - the caller owns that clock,
 * because only the caller knows what "the subject" is (a shelf cursor moving,
 * a menu selection changing).
 *
 * IT WAITS BEFORE IT MOVES, and that is the whole design. Started immediately
 * it is motion competing with a coverflow already easing under the player's
 * thumb - worse than a clipped title. Waiting means flicking through a shelf
 * never triggers it and stopping on a game always does, which turns it from
 * decoration into an answer to a question just asked by stopping.
 *
 * Ping-pong rather than a loop: a loop needs a gap and a wrap, and on a short
 * string you cannot tell where the title ended and restarted. This rests at
 * the beginning, so the idle state is always canonical, and wherever it rests
 * the window ends between two letters (src/linebreak.h's lb_edges), never
 * through one: a sliver of the next letter at the end reads as damage.
 *
 * Costs nothing this launcher was not already paying. Every screen redraws
 * every frame, and ui_text caches the rendered texture per (font, string,
 * color) - so sliding it is a moving destination rect, not a re-render. */
void ui_text_marquee(SDL_Renderer *r, TTF_Font *f, const char *s,
                     int x, int y, int w, unsigned phase, SDL_Color col);

/* The same travel, but one way and round again: hold at the top, scroll at the
 * same pace to `cycle`, then start over from nothing.
 *
 * For a body that REPEATS - the caller lays the content out twice with a rule
 * between the copies, so the wrap at `cycle` lands on a picture identical to
 * the one at zero and cannot be seen. Ping-pong is right for a row of text too
 * wide for its column, where running backwards is obviously a rewind; it is
 * wrong for fifteen lines of prose, where it means reading the end backwards
 * to get to the beginning.
 *
 * The hold happens once a lap, at the top, which is where a reader wants it. */
int ui_scrollthrough(int cycle, unsigned phase);

/* The panel border, in pixels: DESIGN.md's frame, the 3-unit line of **edge**
 * around a surface. In the header because layout outside ui.c has to know it:
 * centering anything inside a panel means centering against the INNER edge,
 * since the border is a visible frame and the eye reads the space within it.
 * Centering against the outer edge is arithmetically right and looks high by
 * exactly this many pixels - which mattered most when this was 12. */
#define UI_PANEL_BORDER DS_FRAME

/* What lifts something off the ground, DESIGN.md 7. One call for both
 * themes, because which of the two a theme lifts with is the theme's
 * business and not the screen's:
 *
 *   Salt & Pepper: an additive radial glow in `sys_rgb`, the system's color,
 *   centered on `q` and spilling past it at the kind's DS_GLOW alpha and
 *   spread - what tells you which card has focus without a frame around
 *   anything, and what the launcher has always done. Light added to a light
 *   ground is all but invisible, which is why Beard does not glow.
 *
 *   Beard: a soft shadow of **ink** under `q`, neutral and never tinted, at
 *   the kind's DS_SHADOW opacity, blur and drop. Except UI_LIFT_WASH, which
 *   is not under anything: it is the system's light along the bottom of the
 *   shelf, and on Beard it is `sys_rgb` laid on at DS_WASH_LIGHT_A with an
 *   ordinary blend, the same shape as the glow.
 *
 * `sys_rgb` may be CFG_NO_COLOR: a system with no color, or a panel that is
 * no system's, glows in **text-dim**, a neutral light, as the shadow is a
 * neutral dark.
 *
 * Drawn BEFORE what it lifts, which covers the middle of it: the shadow is
 * the whole blurred rect, not a ring, and only what shows past `q` reads. */
enum {
	UI_LIFT_FOCUS,         /* a card on the shelf */
	UI_LIFT_FOCUS_SMALL,   /* a thumbnail, Muse's cover */
	UI_LIFT_PANEL,         /* a menu panel, the keyboard */
	UI_LIFT_WASH,          /* the system's light along the bottom of the shelf */
	UI_LIFT_KINDS
};
void ui_lift(SDL_Renderer *r, const SDL_Rect *q, unsigned sys_rgb, int kind);
/* ui_lift at `k` of its strength, 0 to 1: the glow's alpha or the shadow's
 * opacity times `k`, and nothing at 0. For a card on the shelf, which comes
 * into focus and leaves it by degrees as the row moves, and takes its lift
 * with it (cf_draw's lift). ui_lift is this at 1, at full strength. */
void ui_lift_k(SDL_Renderer *r, const SDL_Rect *q, unsigned sys_rgb, int kind, float k);
/* The same lift as theme `theme_ix` (DS_LIGHT or DS_DARK) lifts, whichever
 * theme is on: for what is drawn straight on scrim.game, which is dark in
 * both themes, where Beard's ink shadow would fall on ink and lift nothing
 * (a save slot's picture). */
void ui_lift_in(SDL_Renderer *r, const SDL_Rect *q, unsigned sys_rgb,
                int kind, int theme_ix);

/* A system's color as a mark - a rail, a menu's heading rule, a slot's dot -
 * as 0xRRGGBB, DESIGN.md 3.3: as systems.cfg has it in Salt & Pepper, and a
 * quarter of the way toward **text** on Beard, so a yellow like Game Boy
 * Color's still reads on a light ground. Every mark of a system's color goes
 * through this, so there is one copy of that rule, src/sysmark.c's.
 *
 * Two colors are not a system's own and are said by role instead:
 *   lime, the **accent**, which Muse and Favorites wear because they are
 *   ZyperOS's, is **accent-ink**, lime's own mark (the fill's lime is 1.3:1
 *   on Beard's ground);
 *   CFG_NO_COLOR (src/config.h), a system whose line names no color, is
 *   **text-dim** in both themes, a mark that says where and not whose. */
unsigned ui_sys_mark(unsigned rgb);
/* The same mark on a ground of theme `theme_ix` (DS_LIGHT or DS_DARK),
 * whichever theme is on: for what is drawn straight on scrim.game, which is
 * dark in both themes (the save slots' dots). */
unsigned ui_sys_mark_in(unsigned rgb, int theme_ix);

/* The scrim under a menu, the keyboard or a message, DESIGN.md 3.4: **ink**
 * over the whole target at the theme's scrim.menu, or at scrim.game for a
 * panel over a paused game (`game` true), which is as heavy in both themes
 * because a game is content and not part of the theme. */
void ui_scrim(SDL_Renderer *r, int game);
/* ui_scrim at `k` of its strength, 0 to 1, and nothing at 0: the scrim under
 * a menu's panel as the first one opens or the last one closes (DESIGN.md
 * 8.2), which comes and goes with it. ui_scrim is this at 1. */
void ui_scrim_k(SDL_Renderer *r, int game, float k);

/* The color of item `index` on a rail, so its marker can be a window onto a
 * strip of every item's color rather than a bar that changes all at once. */
typedef unsigned (*ui_rail_hue)(void *ctx, int index);

/* The position rail across the bottom: a full-width line of **track** with a
 * segment of the system's color showing where the shelf sits in a list of
 * `count` (DESIGN.md 11.2).
 *
 * The segment is a MARK, so the rail draws each color through ui_sys_mark
 * itself: a caller passes the color as systems.cfg has it, CFG_NO_COLOR
 * included, in both themes, and does not mix it first.
 *
 * `index` is a FLOAT and wants cf.pos, the shelf's own continuous position -
 * not the cursor, which is already at the destination on the frame of the
 * press. Whole values give the resting positions exactly. See rail_at.
 *
 * `hue` is optional and wins where it is given: the marker is then colored by
 * what it is OVER rather than by `rgb`, so a move carries one system's color
 * into the next instead of swapping it. Pass NULL, and `rgb` alone, for a list
 * whose items all share a color - every games rail, since a game is drawn in
 * its system's accent. */
void ui_rail(SDL_Renderer *r, int screen_w, int screen_h, float index, int count,
             unsigned rgb, ui_rail_hue hue, void *ctx);


/* A filled rounded rectangle. SDL has no such primitive; this is the middle as
 * one rect and the two caps as one inset row each, so a panel costs a few
 * dozen fills rather than one per scanline. */
void ui_round_rect(SDL_Renderer *r, const SDL_Rect *q, int radius, SDL_Color col);

/* A menu slab, DESIGN.md 11.3: the theme's **surface**, opaque, so a list
 * drawn on it reads against a paused game frame or a lit shelf without either
 * showing through, inside a UI_PANEL_BORDER frame of **edge**.
 *
 * The frame is **edge** in both themes, whatever menu it is: a frame in the
 * system's color around a lime focus row doubled the accent, so a system's
 * menu says which system it is on its heading rule instead, and the panel
 * takes no color at all. */
void ui_panel(SDL_Renderer *r, const SDL_Rect *q, int radius);

/* A card for a game with no art: a slab with the title on it and the
 * system's color across the top, DESIGN.md 11.1. Owned by the caller.
 *
 * Drawn in the theme that is on, and baked into the texture: a card made in
 * Beard is Beard's for as long as it lives. So it is marked with the theme it
 * was made for (ui_card_theme), and dropped when another comes on. */
SDL_Texture *ui_make_card(SDL_Renderer *r, const char *title, unsigned rgb,
                          int *w, int *h);
/* The same, square and square-cornered, for an album with no cover. */
SDL_Texture *ui_make_cover(SDL_Renderer *r, const char *title, unsigned rgb,
                           int *w, int *h);
/* A folder's card on a shelf (src/shelfdir.h): the game card's slab and
 * band, the pixel folder (ui_card_folder_art) with how many games it holds
 * on a label, and its name under it. */
SDL_Texture *ui_make_folder_card(SDL_Renderer *r, const char *title, unsigned rgb,
                                 int games, int *w, int *h);
/* The folder a folder's card draws: the system sprites' FOLDER still, a
 * 128-unit picture, handed over (this file frees it) once on the frame
 * thread before any folder card is asked for. Without it a folder's card has
 * its label alone. */
void ui_card_folder_art(SDL_Surface *still);

/* The theme a card's texture was made for, DS_LIGHT or DS_DARK - a card from
 * this file - or -1 for any other texture: art that is no theme's. With cardgen_kept (src/cardgen.h), what
 * decides whether it may still be shown. */
int ui_card_theme(SDL_Texture *t);

/* ---- the generated card, made off the frame ----
 *
 * Drawing a card is several times what putting it on the GPU is (0.24 ms
 * against 0.04 on an M3), and landing on a shelf of games with no art asks
 * for eleven at once. So the art workers (src/texload.h) draw it where there
 * is no file, and the frame only makes its texture.
 *
 * An order is everything a card is drawn from, copied, so a worker reads
 * nothing of the launcher's while the frame goes on changing it: the title,
 * the system's color, the shape, and the theme and the language's faces that
 * were on when it was asked for. */
#define UI_CARD_TITLE 256
typedef struct {
	char     title[UI_CARD_TITLE];
	unsigned rgb;        /* the system's color, or CFG_NO_COLOR */
	bool     album;      /* an album's square cover, not a game's card */
	int      theme_ix;   /* DS_LIGHT or DS_DARK: the theme it is drawn in */
	int      lang_set;   /* whose faces a Chinese or Japanese title is in */
	int      folder;     /* a folder's card, holding this many games; 0 a game's */
} ui_card_order;

/* An order for a card in the theme and language that are on: on the frame. */
void ui_card_order_set(ui_card_order *o, const char *title, unsigned rgb,
                       bool album);
/* The same for a folder holding `games` games (ui_make_folder_card) */
void ui_card_order_folder(ui_card_order *o, const char *title, unsigned rgb,
                          int games);

/* The card the order describes, as an ARGB8888 surface the caller frees: a
 * game's 512 x 656 with round corners, an album's 512 x 512 with square ones.
 *
 * `away` true: in faces of the workers' own, on any thread; NULL when those
 * faces cannot be had - the fonts are down around a fallback game (ui_quit
 * to ui_init), or would not open - and then the card is the frame's to make,
 * as it always was. `away` false: in the frame thread's own faces, on the
 * frame thread. The same pixels either way, which tools/cardpaint-check.c
 * holds; ui_make_card makes them in the workers' faces when it can. */
SDL_Surface *ui_card_paint(const ui_card_order *o, bool away);

/* A card's surface as its texture, blended for its corners and marked with
 * the theme it was drawn in (ui_card_theme). On the frame thread. */
SDL_Texture *ui_card_upload(SDL_Renderer *r, SDL_Surface *s, int theme_ix);

/* The play modes' marks, in the shapes everybody already reads: two arrows
 * chasing round a loop for repeat, the same with a 1 in it for repeat one, and
 * two crossing arrows for shuffle. Drawn here from a few lines of geometry
 * rather than taken from a font or an icon set. Centered on (cx, cy), `size`
 * pixels square, in `col`; each shape and size is rendered once and kept. */
/* And the three answers when a computer is plugged in (DESIGN.md 11.18): a
 * bolt for Charge Only, a folder with an arrow up out of it for File
 * Transfer, a terminal's prompt for Developer (SSH). And the empty home's two
 * ways (11.19): the USB trident, and three arcs and a dot for Wi-Fi. And the
 * answers while File Transfer's files move (11.14): an arrow up and one down
 * for keep going, a page with a tick for stop after this file, a ring crossed
 * for cancel now, a play triangle for play after this file. And a tick for
 * the choice that is on in a list of choices (MENU > Language, 11.30). And
 * the two answers when something on the Pixel 2's port wants power (11.36):
 * headphones for a USB accessory, a laptop for a computer. And Flash
 * Drive's two ways (11.18): an arrow down into a tray, to the card, and up
 * out of it, from the card. */
typedef enum {
	UI_GLYPH_REPEAT, UI_GLYPH_REPEAT_ONE, UI_GLYPH_SHUFFLE,
	UI_GLYPH_CHARGE, UI_GLYPH_TRANSFER, UI_GLYPH_TERMINAL,
	UI_GLYPH_USB, UI_GLYPH_WIFI,
	UI_GLYPH_ARROWS, UI_GLYPH_FINISH, UI_GLYPH_CANCEL, UI_GLYPH_PLAY,
	UI_GLYPH_CHECK,
	UI_GLYPH_HEADPHONES, UI_GLYPH_LAPTOP,
	UI_GLYPH_TO_CARD, UI_GLYPH_FROM_CARD,
	UI_GLYPH_COUNT
} ui_glyph;
void ui_glyph_draw(SDL_Renderer *r, ui_glyph g, int cx, int cy, int size,
                   SDL_Color col);

/* A button drawn rather than named, DESIGN.md 11.9: a circle for the face
 * buttons A, B, X and Y, and a pill for every other (START, MENU, L1/R1),
 * centered on (cx, cy).
 *
 * `size` is the glyph's height in units, and everything else is 11.9's
 * proportions at that height: the face button is a circle `size` across with
 * its letter in the title face at DS_TYPE_TITLE * size / 96 - 11.9's 96 with
 * the title's 60 - and a pill is `size` high with its label at
 * DS_TYPE_TITLE * size / 128, which is half the title at 11.9's 64-unit pill
 * (the mock kit's 30), since a pill carries a word where a face carries a
 * letter. The ring is 11.9's 4 at either one's full height, and a pill's ends
 * are the kit's 30 at 64 (.g-pill), narrowing to its keyboard pills' 0.35 of
 * the height (.kg) at 44 and below. So the tutorial's glyphs are 96 and 64,
 * the keyboard's action keys 40 and the line under them 44. The letter is
 * SemiBold on a face and on the keyboard's glyphs, and Regular on the 64
 * pill, as the kit draws .g-face, .kg and .g-pill.
 *
 * UI_BUTTON_PLAIN is **raised** in an **edge** ring, its letter in **text**.
 * UI_BUTTON_ON_ACCENT is for a glyph on the **accent** fill, a selected key:
 * outlined and lettered in **on-accent**, filled with the accent darkened a
 * little toward it, as the keyboard lab draws it. The pressed glyph and the
 * ones that are no letter are below. */
enum { UI_BUTTON_PLAIN, UI_BUTTON_ON_ACCENT };
void ui_button(SDL_Renderer *r, const char *label, int cx, int cy, int size,
               int style);
/* How wide ui_button draws `label` at `size`: `size` for a circle, and for a
 * pill the label and its two ends. For laying a glyph out beside a label. */
int ui_button_width(const char *label, int size);

/* The glyph pressed, 11.9: `press` 0 up to 1 down, eased by the caller in
 * **press**, draws it at 92% of its size, its ring from **edge** toward
 * **accent** and a soft **accent** halo round it. `scale` draws the whole
 * glyph that many times its size about its center, for one that pulses (the
 * tutorial's answer to a wrong press); 1, or 0, at rest. `op` is its
 * opacity, for a glyph that fades in with its line. At rest, not pressed and
 * opaque it is ui_button, pixel for pixel. UI_BUTTON_ON_ACCENT does not
 * press: it is a selected key's glyph, and the key is what presses. */
void ui_button_pressed(SDL_Renderer *r, const char *label, int cx, int cy, int size,
                       int style, float press, float scale, float op);

/* The glyphs that are no letter, for the tutorial (11.9), each with its
 * press, `scale` and `op` as ui_button_pressed's. A pressed arm or half fills with
 * **accent** and its arrow or label turns **on-accent**, so the direction
 * shows; the whole glyph presses as far as its most pressed part.
 *
 * The d-pad: a cross `size` across (the tutorial's is 120) with an arrow per
 * arm, `arm` the four in UI_DPAD_* order, or NULL for none pressed. */
enum { UI_DPAD_UP, UI_DPAD_DOWN, UI_DPAD_LEFT, UI_DPAD_RIGHT, UI_DPAD_ARMS };
void ui_dpad(SDL_Renderer *r, int cx, int cy, int size, const float arm[UI_DPAD_ARMS],
             float scale, float op);
/* VOL- and VOL+: one rocker pill `size` high, a half each, VOL- on the left,
 * with a 2-unit **edge** line between them. `down` and `up` are each half's
 * press. ui_rocker_width is its width at `size`. */
void ui_rocker(SDL_Renderer *r, int cx, int cy, int size, float down, float up,
               float scale, float op);
int ui_rocker_width(int size);
/* POWER: a face's circle `size` across with the power mark in **text**. */
void ui_power(SDL_Renderer *r, int cx, int cy, int size, float press, float scale,
              float op);

/* rgb interpolation, for easing the background tint between systems */
unsigned ui_mix(unsigned a, unsigned b, float t);

#endif
