/* What a menu row is. No SDL, no renderer, nothing device-specific.
 *
 * Separated from main.c on purpose. A screen's build function must be
 * callable with no renderer and no device, so that "given this state, what
 * does this menu contain" is answerable by `make check` - and that is only
 * true if the types it needs can be included without dragging SDL in with them.
 */
#ifndef ZYPEROS_MENU_H
#define ZYPEROS_MENU_H

#include <stdbool.h>

typedef struct {
	const char *label;
	const char *value;  /* the right column, or NULL for a row that is only a label */
	bool live;          /* false: a placeholder, drawn quiet and doing nothing */
} menu_row;

/* A value that is a tick at the row's right, in the value's color: the
 * choice that is on in a list of choices (MENU > Language). Known by its
 * address, which src/menusnap.c keeps as it is. */
extern const char MENU_TICK[];

/* A value that is X's glyph at the row's right (DESIGN.md 11.9, the glyph the
 * hints draw), on the fill as a selected key's: X does what the row does
 * from anywhere on its page, and the glyph teaches it. The in-game menu's
 * Quit (11.28). Known by its address, as MENU_TICK is. */
extern const char MENU_KEY_X[];

/* A row's label that is buttons, not words: MENU_KEYS_MARK and then the
 * buttons by their ids' names (src/ds/button.h), "+" for together, "," and
 * a space for one after the other, "/" for either ("\x1bKSELECT+R1",
 * "\x1bKMENU, X"), drawn as this handheld's glyphs (DESIGN.md 11.9) at
 * MENU_KEYS_SIZE where the label goes, the value at the right as ever. The
 * Controls pages' (src/controls.c, ctl_glyph_rows). Text, so a panel's
 * snapshot keeps it as it keeps any label. */
#define MENU_KEYS_MARK "\x1bK"
#define MENU_KEYS_SIZE 44
bool menu_keys_is(const char *label);
/* The buttons after the mark, or `label` itself when it has none */
const char *menu_keys_names(const char *label);

/* Y's glyph at a row's right with On or Off before it: a setting Y toggles
 * from anywhere on its page, its state still read (the details page's
 * Favorite, 2026-11-09). Known by their addresses, as MENU_KEY_X is. */
extern const char MENU_KEY_Y_ON[];
extern const char MENU_KEY_Y_OFF[];

/* How a row answers left, right and A (DESIGN.md 9). The menu runner plays
 * the sound on the press, before the screen's handler: a handler may take a
 * while - the in-game menu's Display waits up to 150 ms on Diatom's new
 * picture - and a press is answered at once or not at all. A row with none of
 * these plays accept for A, and for left and right move or edge by whether
 * its value changed, which can only be heard after the handler. */
#define MENU_SND_RING  1  /* left and right step its value round: move, always */
#define MENU_SND_NO_A  2  /* A does nothing on it: deny */
#define MENU_SND_OWN   4  /* its screen plays its sounds itself (UI Sounds) */

/* Row `sel`'s, from the classes a build set beside its rows, by the row's
 * place and never by its text: a Wi-Fi network or a headset is named by its
 * owner, and one called "Core" is still a network. NULL - a screen whose
 * build sets none, as the Wi-Fi and Bluetooth screens' do not - is every row
 * plain, and so is a place past the rows. Pure. */
unsigned menu_sound_at(const unsigned char *sound, int n, int sel);

/* An option row's choices (DESIGN.md 9, 1.0.6). One rule for every option
 * row: more than two choices and A opens their list, the one that is on
 * ticked and the cursor on it, A choosing and B leaving it as it was; two
 * and A turns it over; Left and Right step it on every row, as its screen
 * has them do. Set by a build beside its rows, by place, as the sound
 * classes are; a row with `n` 0 is no option row and its screen has A.
 *
 * The runner applies a choice by sending the row's own Left or Right as
 * many times as it is away (menu_choice_steps), so a screen states its
 * choices in the order its Left and Right go and needs no code to apply
 * one. `own`: the row opens a list of its own on A (Language, Time Zone, a
 * Controls button), which the check counts and the runner leaves to its
 * screen. `none`: no choice is on as a whole (a shader or a strength each
 * system has its own of): nothing ticked, `cur` still where Left and Right
 * go on from, and a choice of that one applied all the same. */
#define MENU_CHOICES_MAX 255   /* a list's most (Menu Music's tracks) */
typedef struct menu_choice {
	unsigned char n;             /* how many; 0 none */
	unsigned char cur;           /* the one that is on, or that steps go on from */
	bool own;                    /* opens its own list on A */
	bool none;                   /* none of them is on as a whole */
	const char *const *labels;   /* their words, n of them (NULL with own) */
	const char *const *notes;    /* what each one does, shown under the list for the one under
	                              * the cursor (Display Mode's); NULL for none */
	/* A picture of what choice `k` does, drawn by its screen in the box
	 * x, y, w x h (units) at the top of a column at the list's left, its
	 * note under it in place of under the list (the Display list's mini
	 * screen: menu_pick, DESIGN.md 11.3). `app` is the app. NULL: none.
	 * With `hover` too, both (Theme's Folder Color). */
	void (*preview)(void *app, int k, int x, int y, int w, int h);
	/* The choice under the cursor shown on the whole screen while the list
	 * is open, with no picture: Night Warmth's list warms the screen as the
	 * cursor moves (DESIGN.md 11.42). Called each pass
	 * with the cursor's choice, and with -1 once the list has closed,
	 * whichever way, before a choice is applied. NULL: none. */
	void (*focus)(void *app, int k);
	/* What the cursor shows on the whole screen while the list is open
	 * (src/ds/hover.h, DESIGN.md 11.3): the choice under it shown once a
	 * move and the kept one put back as the list closes, whichever way;
	 * nothing saved until A. Theme's lists (1.0.6). The hover's ctx is the
	 * app. NULL: none. */
	const struct ds_hover *hover;
	/* Two choices whose list opens on A all the same, for the picture and
	 * the note that tell them apart (Timing, DESIGN.md 9); Left and Right
	 * still turn the row over */
	bool list;
} menu_choice;

/* Row `sel`'s choices from a build's table, or NULL: no table, a place past
 * the rows, or a row that is no option row. Pure. */
const menu_choice *menu_choice_at(const menu_choice *choices, int n, int sel);

/* How the runner gets from row `c`'s choice to choice `to`: the key to send
 * (Right when `right`, else Left) and how many times, straight there and
 * never round past an end, so a row that stops at its ends gets there as
 * surely as one that goes round. 0: it is on already, except with `none`,
 * where Right and back (2, the runner sending Left the second time) applies
 * it to all. Pure. */
int menu_choice_steps(const menu_choice *c, int to, bool *right);

/* A row that is only a rule, separating a list from a footer that is not part
 * of it. Drawn with the same bar the heading uses, so the two cannot drift
 * apart - which is why it belongs to the menu code and not to a screen painting
 * its own line.
 *
 * Not live, so any screen that steps over dead rows skips it for free. */
#define MENU_RULE ((menu_row){ NULL, NULL, false })

/* A note under the rule: a key legend, a count, a caption. Centered, because it
 * describes the list rather than being an item in it - left-aligned it read as
 * one more row you had failed to be able to select.
 *
 * Marked by a sentinel in `value` rather than a new struct field, so every
 * existing { label, value, live } initializer stays valid.
 *
 * A BODY row is the same row LEFT-ALIGNED: one line of a wrapped paragraph
 * rather than a caption. Centering is right for a caption and wrong for prose -
 * across the fifteen-odd lines of a game synopsis every line begins somewhere
 * different and the eye has to hunt for the start of each one. Everything else
 * about the two is identical, so ROW_IS_NOTE covers both and only the draw
 * asks which it has. */
#define MENU_NOTE_MARK ((const char *)1)
#define MENU_BODY_MARK ((const char *)2)
#define MENU_NOTE(s)   ((menu_row){ (s), MENU_NOTE_MARK, false })
#define MENU_BODY(s)   ((menu_row){ (s), MENU_BODY_MARK, false })
#define ROW_IS_BODY(r) ((r).value == MENU_BODY_MARK)
#define ROW_IS_NOTE(r) ((r).value == MENU_NOTE_MARK || \
                        (r).value == MENU_BODY_MARK)

/* A rule INSIDE a body, rather than the one that divides a list from its
 * footer. Drawn identically - same bar, same width - and it exists as its own
 * kind for one reason: MENU_RULE is what tells a scrolling card where its
 * pinned footer starts, so a rule used as punctuation in the middle of some
 * prose would pin everything after it.
 *
 * What it punctuates is the seam of a synopsis that scrolls through and
 * repeats: text, air, rule, air, and the text again. */
#define MENU_HR_MARK ((const char *)3)
#define MENU_HR      ((menu_row){ NULL, MENU_HR_MARK, false })
#define ROW_IS_HR(r) ((r).label == NULL && (r).value == MENU_HR_MARK)

/* Move to the next live row in `dir` (+1 or -1), or stay put if none is.
 *
 * It does not wrap: Down on the last live row and Up on the first stay where
 * they are. That is what a HELD direction does at an end (DESIGN.md 11.3): it
 * stops there, so a list is never run past by a thumb left on the d-pad.
 * Bounded too - a menu can legitimately be all dead rows ("No networks
 * found" under a rule is exactly that), and the walk stops at the ends of the
 * list. Pure, so tools/menu-check.c holds it to both. */
int menu_step_sel(const menu_row *rows, int n, int sel, int dir);

/* The same step, going round: Down on the last live row goes to the first,
 * Up on the first to the last. What a fresh PRESS does (DESIGN.md 11.3, my
 * pick 2026-11-12: lists go round, a held direction stops at the end and the
 * next press goes on). A list with no live row but `sel` stays put. Pure. */
int menu_step_round(const menu_row *rows, int n, int sel, int dir);

/* Which way a list goes on a press: round for a fresh press, stopping at the
 * end for a held one. The one rule, for the loops that keep a plain index
 * (0 to n - 1) rather than rows: `fresh` is the press, not its repeat. */
int menu_step_index(int n, int sel, int dir, bool fresh);

/* ---- a list that goes round, drawn (DESIGN.md 11.3) ------------------------
 *
 * A list longer than its window does not jump back to its top when it goes
 * round: it goes on. After its last row comes a SEAM, a row's height with the
 * heading's rule across it, and then its first row again, and so on either
 * way without end. A place on that endless list is a SLOT: slot s holds row
 * s mod (n + 1), and the one that comes to n is the seam. The window and the
 * cursor are slots, so a step across the seam is two slots, as small a move
 * as any, and the list scrolls through it as it scrolls through rows. */

/* The row slot `slot` holds, or -1 for the seam. */
int menu_loop_row(int n, int slot);

/* The slot of row `sel` nearest to slot `near`: where the cursor is now,
 * given where it was. A step lands one slot on, and a wrap two (over the
 * seam) rather than the length of the list back. */
int menu_loop_slot(int n, int sel, int near);

/* The window's first slot, as menu_window_first keeps it: it moves only when
 * the cursor's slot would leave it, and the dead rows directly above the
 * cursor come with it. No ends to clamp to. */
int menu_loop_first(const menu_row *rows, int n, int slot, int vis, int first);

/* Which row a scrolling list should draw first, given where it drew first last
 * time. `vis` is how many rows fit; `first` is the previous answer, which is
 * kept unless the cursor has moved out of it.
 *
 * Here rather than inside menu_draw because the rule has a case that only shows
 * up after a particular walk through a particular list - down to the bottom and
 * back - which is exactly the shape of thing a check can hold and a person
 * looking at one frame cannot. Pure, so tools/menu-check.c can walk it. */
int menu_window_first(const menu_row *rows, int n, int sel, int vis, int first);

/* Whether a row too wide for its panel SCROLLS rather than being cut short.
 *
 * Two reasons to move: the cursor is on it, or the cursor can never get to it -
 * because a row nobody can select has no other way of showing text that does
 * not fit. `visits_all` is how a caller says the second reason does not apply
 * to its list, which is true wherever `live` means something other than
 * selectable.
 *
 * Pure, and here rather than inside the renderer, because the last time this
 * rule quietly meant something else nobody could see it: the achievement list
 * scrolled a title or cut it depending on whether the player had EARNED it. */
bool menu_row_moves(menu_row row, bool selected, bool visits_all);

/* Whether the cursor has a row to land on: a live one, or any with
 * `visits_all`. A list with none is a message, not a choice, and the panel
 * draws no focus plate on it (menu_panel) */
bool menu_rows_landable(const menu_row *rows, int n, bool visits_all);

#endif
