/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_TOUR_H
#define ZYPEROS_TOUR_H

/* The tutorial, DESIGN.md 11.10: the first thing ZyperOS shows on a handheld
 * whose device database has `tour.done` at 0 (seeded so: tour_wanted), and
 * again from MENU > Show Tutorial. Zyper says hello, and then each button the handheld has is a step
 * of its own: a small loop on the stage shows what the button does, the step
 * waits for the player to press it, and the press is rewarded - Zyper comes
 * up from the bottom edge, hops and cheers, a lime check pops, the progress
 * strip fills, the reward sound plays - and the next step comes on by itself.
 * START skips the rest at any point.
 *
 * Ten steps, one per thing to learn, in the order a player needs them: move,
 * open (A), back (B), details (X), favorite (Y, on the details since
 * 2026-11-08, when Y on the shelf became "from the start"), settings (MENU),
 * music (SELECT), volume, brightness and power. Each is a press, not a reading, so
 * the minute it takes is spent with a thumb on the button rather than on a
 * page of text; MENU > Controls stays the list to look things up in. The
 * shoulders ride on two of them as a note, with no step of their own: L1/R1
 * jump in Move Around, where they count as moves, and L2/R2 change the song
 * in Music.
 *
 * Then four choices make the handheld the player's (2026-11-06, and
 * his picks in the onboarding lab): the theme, the home screen's layout, the
 * UI sounds and the menu music, each the choices of a MENU row side by side.
 * LEFT and RIGHT look at them, the caller applying the focused one at once
 * so it is seen and heard, and A keeps it, which the caller saves to MENU's
 * own setting. The menu music's step is left out on a card with no tracks.
 *
 * This file is the steps and their clocks: what each button is to the
 * tutorial and which part of a glyph the player's hand holds, what each step
 * waits for, what a press plays, when the next comes on, where a
 * demonstration loop is at any moment, how far each part of the button glyph
 * is pressed, and where Zyper is - his moves from src/zyperrig.c's rig, the
 * menu's, driven by the tutorial's events. No SDL, so tools/tour-check.c
 * drives it. src/tourdraw.c draws it, a pure function of this state and the
 * clock; src/main.c reads the buttons, plays the sounds and changes the real
 * volume and brightness.
 *
 * As I picked it in the tutorial lab (2026-11-04): the welcome and the
 * finish are Zyper's own words in his bubble; during a step Zyper is there
 * only for the reward; and a press of the wrong button gets the deny sound,
 * Zyper's head cocked, the right glyph pulsing twice and his bubble saying
 * which button that was and which to try.
 *
 * Times are milliseconds on any clock that only goes forward (plat_now_ms,
 * or a shot's chosen moment). Every comparison is of a difference, so the
 * clock may wrap. */

#include <stdbool.h>
#include <stdint.h>

#include "db.h"
#include "i18n_ids.h"   /* LANG_COUNT: the languages the first step offers */
#include "inbutton.h"   /* the launcher's buttons, with no SDL */
#include "zyperrig.h"   /* Zyper's moves, one copy for the menu and here */

typedef enum {
	TOUR_LANG,
	TOUR_WELCOME,
	TOUR_MOVE, TOUR_A, TOUR_B, TOUR_X, TOUR_Y,
	TOUR_MENU, TOUR_SELECT, TOUR_VOL, TOUR_BRIGHT, TOUR_POWER,
	TOUR_THEME, TOUR_HOME, TOUR_SOUNDS, TOUR_MUSIC,
	TOUR_FINISH,
	TOUR_STEPS
} tour_step;

/* The first step asks for the language (DESIGN.md 11.30; my picks in
 * the languages lab, 2026-11-07): Zyper as on the welcome, his bubble saying
 * hello in the language under the cursor, the languages beside him, each in
 * its own words. Until a press, the cursor steps to the next language every
 * TOUR_LANG_STEP_MS, so the screen greets in each in turn (My note);
 * any press stops it there. UP and DOWN (LEFT and RIGHT too) move it, A
 * keeps it and goes on to the welcome, in it. START skips the rest, keeping
 * the one under the cursor if UP or DOWN put it there, else the one that was
 * on: going round alone chooses nothing. The caller draws each language as
 * the cursor reaches it (tour_lang_focus) and saves tour_lang_kept. */
#define TOUR_LANGS        LANG_COUNT
#define TOUR_LANG_STEP_MS 1000

/* The ten steps that teach a button. */
#define TOUR_FIRST_LESSON TOUR_MOVE
#define TOUR_LESSONS      (TOUR_POWER - TOUR_MOVE + 1)

/* The four that ask, after them. The progress strip counts both: 14, or 13
 * on a card with no menu music. */
#define TOUR_FIRST_CHOICE TOUR_THEME
#define TOUR_CHOICES      (TOUR_MUSIC - TOUR_THEME + 1)
#define TOUR_CHOICE_MAX   4    /* the most any of them offers: the sounds' */

/* Which choice a step is, 0 to TOUR_CHOICES - 1, or -1 for a step that is
 * not one. */
int tour_choice_of(tour_step s);
/* How many choices a choice step offers, 0 for another; and each one's name
 * as it is drawn under it, which is MENU's row value: the theme 0 Light, 1
 * Dark; the home screen 0 Spotlight, 1 Gallery; the sounds 0 Wood, 1 Glass,
 * 2 Bubble, 3 Off (uisound's setting order); the music 0 On, 1 Off. */
int         tour_choice_count(tour_step s);
const char *tour_choice_name(tour_step s, int i);

/* A press, as the launcher reads it: one call of tour_press each. VOL and
 * BRIGHT are either direction, whatever the device calls them (the Pixel 2's
 * MENU+VOL, the Brick's F1 and F2). */
typedef enum {
	TK_UP, TK_DOWN, TK_LEFT, TK_RIGHT,
	TK_A, TK_B, TK_X, TK_Y, TK_L1, TK_R1, TK_L2, TK_R2,
	TK_START, TK_SELECT, TK_MENU,
	TK_VOL, TK_BRIGHT, TK_POWER,
	TK_KEYS
} tour_key;

/* What a launcher button is to the tutorial: the d-pad's four, the faces,
 * the shoulders (L1/R1 and L2/R2, each a wrong press but in no step bar L1/R1's Move Around), START, SELECT, MENU,
 * both volume keys as TK_VOL, both brightness keys as TK_BRIGHT, POWER.
 * TK_KEYS for IN_NONE or anything out of range. */
tour_key tour_key_of(in_button b);
/* The button's own name, as "That's X." says it in English: Up, A, L1,
 * START... English whatever language is on, for the command line's
 * --tour-wrong; the answer itself says it in the language that is on. */
const char *tour_key_name(tour_key k);

/* The parts of the glyphs that can be seen pressed, by the demonstration or
 * by the player's own hand. The shoulders have no glyph: the handheld's are
 * what shows them pressed (src/tourdevice.h), lit in the lessons whose notes
 * name them - L1/R1 in Move Around, L2/R2 in Music. */
typedef enum {
	TP_UP, TP_DOWN, TP_LEFT, TP_RIGHT,
	TP_A, TP_B, TP_X, TP_Y,
	TP_START, TP_SELECT, TP_MENU,
	TP_VOLDN, TP_VOLUP, TP_F1, TP_F2,
	TP_POWER,
	TP_L1, TP_R1, TP_L2, TP_R2,
	TP_PARTS
} tour_part;

/* What a step's button is drawn as (DESIGN.md 11.9): a face, a pill, the
 * d-pad, the volume rocker, POWER, or the brightness chord, which is the
 * MENU pill, a plus and the rocker on the Pixel 2 and the F1 and F2 pills on
 * the Brick. */
typedef enum { TG_FACE, TG_PILL, TG_DPAD, TG_ROCKER, TG_CHORD, TG_POWER } tour_glyph;

typedef enum { TC_OUT, TC_IN, TC_TURN, TC_FADE } tour_curve;

/* A step's demonstration: one press of its button `press_at` ms into the
 * loop and let go at `release_at`; what the press does (the loop's `a`, 0 to
 * 1) starting at `act` and taking `dur` on `curve`; held until `hold`, then
 * undone in `back_ms`, either run backward on `back_curve` (`reverse`) or
 * faded out over the start; and round again every `period`. */
typedef struct {
	tour_part  part;
	int        press_at, release_at;
	int        act, dur;
	tour_curve curve;
	int        hold;
	bool       reverse;
	int        back_ms;
	tour_curve back_curve;
	int        period;
} tour_loop;

/* A step's words in the language that is on (src/i18n.h); a glyph's letter
 * or word and the buttons it names are as printed, in every language. */
typedef struct {
	const char *title;
	const char *line;          /* under the title, meta text-soft; the welcome's
	                            * and the finish's are two lines, "\n" between */
	const char *line_fn;       /* the same where brightness is MENU+VOL, or NULL */
	const char *note;          /* a second line, meta text-dim, or NULL */
	const char *note_done;     /* the second line once the press landed, or NULL */
	tour_glyph  glyph;
	const char *label;         /* a face's letter or a pill's word */
	const char *want;          /* "Try %s." in a wrong press's answer */
	const char *want_fn;
	const tour_loop *loop;     /* NULL for a step with a loop of its own */
} tour_info;

const tour_info *tour_step_info(tour_step s);

/* The little shelf on the stage: five systems by their systems.cfg names
 * and accents, the third (SNES) the one in focus, which opens. Content, not
 * chrome, so each is that system's own color; tools/tour-check.c holds the
 * accents to the shipped config/systems.cfg. */
typedef struct { const char *name; unsigned rgb; } tour_little;
#define TOUR_LITTLE_N     5
#define TOUR_LITTLE_FOCUS 2
extern const tour_little TOUR_LITTLE[TOUR_LITTLE_N];

/* How far the corner Zyper is under the bottom edge: 0 up, 1 gone, and on
 * its way from `from` to `to` since t0, in `ms` on `curve`. A new window
 * starts it from where it is, so one window replacing another never snaps
 * him (DESIGN.md 8.2). */
typedef struct {
	float      from, to;
	uint32_t   t0;
	int        ms;
	tour_curve curve;
} tour_rise;

/* The cheer Zyper says for lesson `i` (0 to TOUR_LESSONS - 1). */
const char *tour_cheer(int i);

/* What the step model keeps. Read by the drawing; written only here. */
typedef struct {
	tour_step step;
	uint32_t  step_at;       /* when this step came on */
	bool      done;          /* its press landed */
	uint32_t  done_at;
	float     a0;            /* where its loop was when the press landed */
	int       moves;         /* the move step: presses so far */
	int       pos;           /* where its little shelf is going, -2 to 2 */
	float     pos_from;      /* and where it was coming from */
	uint32_t  moved_at;
	/* The reward of the step before this one, still going as this comes on. */
	bool      had_reward;
	tour_step last_step;
	uint32_t  last_at;
	/* A press of the wrong button, and Zyper's answer to it. */
	bool      wrong;
	uint32_t  wrong_at;
	char      hint[128];     /* room for the longest language's */
	/* Zyper: the rig his moves come from, the corner's rise, whether this
	 * reward's hop and pant were started, and the hello's moment */
	zr_rig    zy;
	tour_rise rise;
	bool      cheered;
	uint32_t  hello_at;
	uint32_t  next_hop;      /* the finish: his next hop */
	bool      over;          /* finished or skipped */
	bool      skipped;
	uint32_t  end_at;
	bool      fn_volume;     /* brightness is MENU+VOL (the Pixel 2), not F1/F2 */
	/* The choices: the one each step has focused, and the one saved, which
	 * a step starts on and a skip goes back to (tour_choices_from). In a
	 * choice step `pos`, `pos_from` and `moved_at` are its focus moving, as
	 * they are the move step's little shelf. */
	int       pick[TOUR_CHOICES];
	int       kept[TOUR_CHOICES];
	bool      music;         /* the card has menu music: tour_set_music */
	/* The language step (TOUR_LANG): the language that was on, the one the
	 * cursor is on once a press stopped them going round, whether a press
	 * did, whether UP or DOWN moved the cursor, and the one kept */
	int       lang_start, lang_pick, lang_kept;
	bool      lang_touched, lang_moved, from_lang;
	/* When the welcome's words come in: his hello's moment, or the moment
	 * the language step handed over to it, Zyper already there */
	uint32_t  say_at;
	struct { bool ever, held; uint32_t down_at, up_at; } hand[TP_PARTS];
} tour;

/* The language that was on when the tutorial started, which the cursor
 * starts on and a skip goes back to (anything out of range: the first) */
void tour_lang_from(tour *t, int lang);
/* The language under the cursor at `now`, 0 to TOUR_LANGS - 1 */
int  tour_lang_focus(const tour *t, uint32_t now);
/* The language to keep, once the step is left or the tutorial skipped */
int  tour_lang_kept(const tour *t);

/* From the welcome, at `now`. `fn_volume`: plat_brightness_on_volume. */
void tour_start(tour *t, uint32_t now, bool fn_volume);

/* One press. Returns the sound it plays (a ds_sound, src/design.h), or
 * TOUR_QUIET for none. VOL and BRIGHT are the levels' keys, which the caller
 * has already turned up or down: they answer their own steps, a VOL in the
 * brightness step is a wrong press, and anywhere else they are not the
 * tour's. POWER is the tour's only while tour_takes_power says so. */
#define TOUR_QUIET (-1)
int tour_press(tour *t, tour_key k, uint32_t now);

/* This frame's presses (in_state's `pressed`), every button but POWER, each
 * to tour_press in the launcher's order, until the tour is over. POWER is the
 * caller's: tour_takes_power says whether it is the tour's, for tour_press
 * with TK_POWER. The sounds they play go into `sounds`; returns how many. */
int tour_buttons(tour *t, const bool pressed[IN_COUNT], uint32_t now, int sounds[IN_COUNT]);

/* Once a frame: a reward that has held its time moves to the next step. */
void tour_tick(tour *t, uint32_t now);

/* The settings as they are, as each choice's index (tour_choice_name), for
 * the choice steps to start on and a skip to go back to. Out of range is 0. */
void tour_choices_from(tour *t, const int at[TOUR_CHOICES]);
/* Whether the card has menu music, so whether the Menu Music step comes:
 * said every pass, as the folder is read on a thread of its own. */
void tour_set_music(tour *t, bool has);
/* The progress strip: how many segments, and which this step is (the
 * finish: all of them, done). The welcome has none. */
int tour_segments(const tour *t);
int tour_segment(const tour *t);
/* Where a choice step's focus is, 0 to its count - 1, easing in **move**
 * from where it was when it last moved. */
float tour_focus_pos(const tour *t, uint32_t now);

/* Whether POWER belongs to the tour rather than turning the handheld off:
 * in the power step, until it has been pressed. */
bool tour_takes_power(const tour *t);

/* A part of the player's own hand, down or up this frame. */
void tour_hand(tour *t, tour_part p, bool down, uint32_t now);
/* Every part from the launcher's buttons as they are held this frame
 * (in_state's `down`). `menu_held` is plat_menu_held: on the Pixel 2 MENU is
 * FUNCTION, which `down` sees only as it is let go. There the brightness
 * keys are FUNCTION held with a volume key, so they light MENU and that half
 * of the rocker; on the Brick they are F1 and F2. */
void tour_hands(tour *t, const bool down[IN_COUNT], bool menu_held, uint32_t now);

/* ---- where the clocks are, for the drawing ----------------------------------- */

/* A motion token's curve, `ms` long, `at` the start, at `now`: 0 before it
 * starts, 1 from its end. */
float tour_ease(tour_curve c, uint32_t now, uint32_t at, int ms);

/* Ms into a loop `period` long, which starts 500 ms after the step does. */
int tour_loop_time(const tour *t, int period, uint32_t now);

/* A step's loop at `now`: `a` what the press does, 0 to 1, and `back`, how
 * far a faded undo has gone over the start. */
typedef struct { float a, back; } tour_loop_at;
tour_loop_at tour_loop_now(const tour *t, uint32_t now);

/* After the press: from wherever the loop was on to the end. */
float tour_real_a(const tour *t, uint32_t now);

/* Where the move step's little shelf is, -2 to 2, now. */
float tour_shelf_pos(const tour *t, uint32_t now);

/* How far part `p` of the glyph is pressed, 0 to 1: by the demonstration
 * while the step waits, and by the player's hand. */
float tour_part_press(const tour *t, tour_part p, uint32_t now);

/* A wrong press's pulse of the right glyph, twice in tour-pulse, dying away:
 * 0 to 1, how far it is grown toward its 6%. */
float tour_wrong_pulse(const tour *t, uint32_t now);
/* Whether the wrong press's answer is still showing: hold-short, then out in
 * fade-out. */
bool  tour_wrong_on(const tour *t, uint32_t now);

/* Zyper at `now`, where he is: in the corner of a step (`corner`), `drop`
 * of the way under the bottom edge, or big for the welcome's and the
 * finish's hello (`hello`), `scale` of his size about his feet as he pops in.
 * Never both: at the finish the corner's leaves in **exit** and the hello
 * waits for it. Neither: the corner is empty (between rewards, and in the
 * power step, whose Zyper is the stage's, until its press), or he has not
 * popped in yet. His pose is the rig's (src/zyperrig.c, the menu's moves:
 * the tilt and tongue, a failure's cocked head, the hop and pant, the blink,
 * the breath) with the hello's ear swing on top. */
typedef struct {
	zr_pose pose;
	bool    corner, hello;
	float   drop;
	float   scale;
} tour_zyper_at;
tour_zyper_at tour_zyper_now(const tour *t, uint32_t now);

/* ---- for --shot -------------------------------------------------------------- */

/* A still: `step`, `ms` into it; its press landed `done_ms` ago, or below 0,
 * not yet. The finish includes the preceding power reward, so its first
 * frames show the corner Zyper leaving before the large hello pops in. */
void tour_pose(tour *t, tour_step step, uint32_t now, int ms, int done_ms,
               bool fn_volume);
/* And a wrong press of `k`, `ms` ago. */
void tour_pose_wrong(tour *t, tour_key k, uint32_t now, int ms);
/* And, in a choice step, the focus moved to `pick` `ms` ago from the one it
 * started on. */
void tour_pose_pick(tour *t, int pick, uint32_t now, int ms);

/* ---- the setting ------------------------------------------------------------- */

/* The device database's key, seeded 0 and set to 1 once the tour is seen:
 * finished, skipped, or past the power step's press (tour_seen). */
#define TOUR_DONE_KEY "tour.done"

/* Whether the tutorial is still to be shown on this handheld: the one rule,
 * here and nowhere else. A database that opened with TOUR_DONE_KEY at 0 -
 * seeded so on a new handheld, and on one that ran a ZyperOS from before the
 * tutorial, which therefore shows it once after the upgrade (My call,
 * 2026-11-04). One that did not open says no, or the tutorial would run at
 * every start with nothing to remember it by. */
bool tour_wanted(db *dev);
/* Whether this tour counts as seen: over (finished or skipped), or past the
 * power step's press - the second POWER in that reward turns the handheld
 * off, and a tour that taught every button is not shown again for it. */
bool tour_seen(const tour *t);
/* Remember a tour that is seen. One that is not - POWER, Auto Off or a quit
 * in the middle of it - is not remembered, and runs again at the next start. */
void tour_save(db *dev, const tour *t);

#endif
