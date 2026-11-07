/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* The tutorial's steps: what each waits for, what a press plays and when the
 * next comes on, and where every clock the drawing reads is. No SDL;
 * src/tourdraw.c draws it. See src/tour.h.
 *
 * Ported from the tutorial lab's model (tour.js), which was ported from the
 * first of this file: the same numbers, so what I pressed through there
 * is what the handheld does. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "clamp.h"
#include "design.h"
#include "i18n.h"
#include "tour.h"

/* ---- the clocks ----------------------------------------------------------- */

/* Presses this early in a step are the last step's thumb still on its way
 * off the button, not an answer to this one. */
#define DEAF_MS        250
/* The finish waits a beat longer, so the A that ends the power step's reward
 * does not skip the party. */
#define FINISH_DEAF_MS 600
/* The move step wants two moves: one can be an accident. */
#define MOVES_WANTED   2
/* A demonstration loop starts this long after its step comes on, so the step
 * is read before its button presses itself. */
#define LOOP_DELAY_MS  500
/* The welcome and the finish: their A presses itself every LOOP_A_MS, from
 * LOOP_A_AT into the screen, as a step's glyph does. */
#define LOOP_A_AT      (120 + 1600)
#define LOOP_A_MS      2800
/* The move step's loop: the shelf steps right and back. */
#define MOVE_LOOP_MS   2800
/* The volume and brightness steps' loop: a stand-in bar comes in and steps
 * up twice. */
#define LEVEL_LOOP_MS  3000

/* ---- the steps ------------------------------------------------------------ */

static const tour_loop LOOP_A = {
	TP_A, 600, 760, 600, DS_MS_LAUNCH, TC_IN, 2500, false, 260, TC_FADE, 3400 };
static const tour_loop LOOP_B = {
	TP_B, 700, 860, 700, DS_MS_MOVE, TC_OUT, 2400, false, 260, TC_FADE, 3200 };
static const tour_loop LOOP_Y = {
	TP_Y, 600, 760, 600, DS_MS_QUICK, TC_OUT, 2200, false, DS_MS_FADE_OUT, TC_FADE, 3000 };
static const tour_loop LOOP_X = {
	TP_X, 600, 760, 600, DS_MS_TURN, TC_TURN, 2600, true, DS_MS_TURN, TC_TURN, 3600 };
/* The menu opens at MENU's release: on the Pixel 2 that is when MENU comes
 * (src/device/pixel2.c, key_event); on the Brick it opens at the press, which
 * tour_loop_now says. */
static const tour_loop LOOP_MENU = {
	TP_MENU, 600, 760, 760, DS_MS_MOVE, TC_OUT, 2400, true, DS_MS_EXIT, TC_IN, 3200 };
static const tour_loop LOOP_SELECT = {
	TP_SELECT, 600, 760, 600, DS_MS_MOVE, TC_OUT, 2400, true, DS_MS_FADE_OUT, TC_FADE, 3200 };
/* anim_poweroff small, its 900 ms after 150 of the shelf going. */
static const tour_loop LOOP_POWER = {
	TP_POWER, 600, 760, 600, 1100, TC_FADE, 2900, false, 300, TC_FADE, 3600 };

/* A step's words, by their IDs (NO_TEXT for none), which tour_step_info
 * says in the language that is on; its glyph's letter or word and the
 * buttons a wrong press's answer names are as printed, in every language. */
#define NO_TEXT S_COUNT

typedef struct {
	str_id      title, line, line_fn, note, note_done;
	tour_glyph  glyph;
	const char *label;
	str_id      want;          /* "Try %s.": words, or NO_TEXT and */
	const char *want_key;      /* a button by its printed name */
	const char *want_fn;
	const tour_loop *loop;
} step_def;

static const step_def STEPS[TOUR_STEPS] = {
	/* The language: his hello in the language under the cursor, the A on
	 * the focused row (src/tourdraw.c) */
	[TOUR_LANG]    = { S_TOUR_LANG_HELLO, NO_TEXT,
	                   NO_TEXT, NO_TEXT, NO_TEXT, TG_FACE, "A", NO_TEXT, "A", NULL, NULL },
	/* The welcome's and the finish's lines are two, "\n" between */
	[TOUR_WELCOME] = { S_TOUR_WELCOME_TITLE, S_TOUR_WELCOME_LINES,
	                   NO_TEXT, NO_TEXT, NO_TEXT, TG_FACE, "A", NO_TEXT, "A", NULL, NULL },
	/* The shoulders as a note on the two lessons they belong with, each
	 * pair lit on the handheld (my follow-up to #90): L1/R1 jump on the
	 * home and count as moves here, L2/R2 change the song - MP4's while it
	 * plays, the menu music's otherwise */
	[TOUR_MOVE]    = { S_TOUR_MOVE_TITLE, S_TOUR_MOVE_LINE,
	                   NO_TEXT, S_TOUR_MOVE_NOTE, NO_TEXT, TG_DPAD, NULL, S_TOUR_TRY_DPAD, NULL,
	                   NULL, NULL },
	[TOUR_A]       = { S_TOUR_A_TITLE, S_TOUR_A_LINE,
	                   NO_TEXT, NO_TEXT, NO_TEXT, TG_FACE, "A", NO_TEXT, "A", NULL, &LOOP_A },
	[TOUR_B]       = { S_TOUR_B_TITLE, S_TOUR_B_LINE,
	                   NO_TEXT, NO_TEXT, NO_TEXT, TG_FACE, "B", NO_TEXT, "B", NULL, &LOOP_B },
	[TOUR_Y]       = { S_TOUR_Y_TITLE, S_TOUR_Y_LINE,
	                   NO_TEXT, NO_TEXT, NO_TEXT, TG_FACE, "Y", NO_TEXT, "Y", NULL, &LOOP_Y },
	[TOUR_X]       = { S_TOUR_X_TITLE, S_TOUR_X_LINE,
	                   NO_TEXT, NO_TEXT, NO_TEXT, TG_FACE, "X", NO_TEXT, "X", NULL, &LOOP_X },
	[TOUR_MENU]    = { S_TOUR_MENU_TITLE, S_TOUR_MENU_LINE,
	                   NO_TEXT, NO_TEXT, NO_TEXT, TG_PILL, "MENU", NO_TEXT, "MENU", NULL,
	                   &LOOP_MENU },
	[TOUR_SELECT]  = { S_TOUR_SELECT_TITLE, S_TOUR_SELECT_LINE,
	                   NO_TEXT, S_TOUR_SELECT_NOTE, NO_TEXT, TG_PILL,
	                   "SELECT", NO_TEXT, "SELECT", NULL, &LOOP_SELECT },
	[TOUR_VOL]     = { S_TOUR_VOL_TITLE, S_TOUR_VOL_LINE,
	                   NO_TEXT, NO_TEXT, NO_TEXT, TG_ROCKER, NULL, S_TOUR_TRY_VOL, NULL, NULL,
	                   NULL },
	/* The Pixel 2's chord is MENU+VOL, as MENU > Controls names it and as
	 * its glyph is drawn, the MENU pill and the rocker */
	[TOUR_BRIGHT]  = { S_TOUR_BRIGHT_TITLE, S_TOUR_BRIGHT_LINE,
	                   S_TOUR_BRIGHT_LINE_FN, NO_TEXT, NO_TEXT, TG_CHORD, NULL,
	                   S_TOUR_TRY_F1_F2, NULL, "MENU+VOL", NULL },
	[TOUR_POWER]   = { S_TOUR_POWER_TITLE, S_TOUR_POWER_LINE, NO_TEXT,
	                   S_TOUR_POWER_NOTE, S_TOUR_POWER_NOTE_DONE,
	                   TG_POWER, NULL, NO_TEXT, "POWER", NULL, &LOOP_POWER },
	/* The choices: no glyph, the choices themselves side by side, and the
	 * row MENU keeps them in named in the note. */
	[TOUR_THEME]   = { S_TOUR_THEME_TITLE, S_TOUR_CHOICE_LOOK, NO_TEXT,
	                   S_TOUR_THEME_NOTE, NO_TEXT, TG_FACE, NULL,
	                   S_TOUR_TRY_CHOICE, NULL, NULL, NULL },
	[TOUR_HOME]    = { S_TOUR_HOME_TITLE, S_TOUR_CHOICE_LOOK, NO_TEXT,
	                   S_TOUR_HOME_NOTE, NO_TEXT, TG_FACE, NULL,
	                   S_TOUR_TRY_CHOICE, NULL, NULL, NULL },
	[TOUR_SOUNDS]  = { S_TOUR_SOUNDS_TITLE, S_TOUR_CHOICE_LISTEN, NO_TEXT,
	                   S_TOUR_SOUNDS_NOTE, NO_TEXT, TG_FACE, NULL,
	                   S_TOUR_TRY_CHOICE, NULL, NULL, NULL },
	[TOUR_MUSIC]   = { S_TOUR_MUSIC_TITLE, S_TOUR_CHOICE_LISTEN, NO_TEXT,
	                   S_TOUR_MUSIC_NOTE, NO_TEXT, TG_FACE, NULL,
	                   S_TOUR_TRY_CHOICE, NULL, NULL, NULL },
	[TOUR_FINISH]  = { S_TOUR_FINISH_TITLE, S_TOUR_FINISH_LINES,
	                   NO_TEXT, NO_TEXT, NO_TEXT, TG_FACE, "A", NO_TEXT, "A", NULL, NULL },
};

static const char *said(str_id id)
{
	return id == NO_TEXT ? NULL : T(id);
}

const tour_info *tour_step_info(tour_step s)
{
	/* A step's own, so two steps' can be held at once (the reward going
	 * and the step coming); said again at every call, a few loads, so a
	 * change of language is the next frame's */
	static tour_info info[TOUR_STEPS];
	const step_def *d;

	if (s < 0 || s >= TOUR_STEPS) s = TOUR_WELCOME;
	d = &STEPS[s];
	info[s] = (tour_info){ said(d->title), said(d->line), said(d->line_fn), said(d->note),
	                       said(d->note_done), d->glyph, d->label,
	                       d->want == NO_TEXT ? d->want_key : T(d->want), d->want_fn, d->loop };
	return &info[s];
}

/* Each choice's names, MENU's row values in the order its setting steps
 * through them. */
static const struct { int n; str_id name[TOUR_CHOICE_MAX]; } CHOICES[TOUR_CHOICES] = {
	{ 2, { S_TOUR_CHOICE_LIGHT, S_TOUR_CHOICE_DARK } },
	{ 2, { S_TOUR_CHOICE_SPOTLIGHT, S_TOUR_CHOICE_GALLERY } },
	{ 4, { S_TOUR_CHOICE_WOOD, S_TOUR_CHOICE_GLASS, S_TOUR_CHOICE_BUBBLE, S_TOUR_CHOICE_OFF } },
	{ 2, { S_TOUR_CHOICE_ON, S_TOUR_CHOICE_OFF } },
};

int tour_choice_of(tour_step s)
{
	return s >= TOUR_FIRST_CHOICE && s < TOUR_FIRST_CHOICE + TOUR_CHOICES
	       ? (int)(s - TOUR_FIRST_CHOICE) : -1;
}

int tour_choice_count(tour_step s)
{
	const int c = tour_choice_of(s);

	return c < 0 ? 0 : CHOICES[c].n;
}

const char *tour_choice_name(tour_step s, int i)
{
	const int c = tour_choice_of(s);

	return c < 0 || i < 0 || i >= CHOICES[c].n ? "" : T(CHOICES[c].name[i]);
}

/* Section 12 allows "!" in Zyper's bubble, and only there. */
static const str_id CHEERS[] = {
	S_TOUR_CHEER_NICE, S_TOUR_CHEER_GOT_IT, S_TOUR_CHEER_WOOF, S_TOUR_CHEER_PERFECT,
	S_TOUR_CHEER_GOOD_ONE,
};
#define CHEERS_N (int)(sizeof CHEERS / sizeof CHEERS[0])

const char *tour_cheer(int i)
{
	return T(CHEERS[(i % CHEERS_N + CHEERS_N) % CHEERS_N]);
}

/* The button's own name, for "That's X.": as printed, in every language;
 * the d-pad's four and the levels' keys have none printed, and are words
 * (KEY_WORDS) */
static const char *const KEY_NAME[TK_KEYS] = {
	[TK_A] = "A", [TK_B] = "B", [TK_X] = "X", [TK_Y] = "Y",
	[TK_L1] = "L1", [TK_R1] = "R1",
	[TK_START] = "START", [TK_SELECT] = "SELECT", [TK_MENU] = "MENU",
	[TK_POWER] = "POWER",
};
static const str_id KEY_WORDS[TK_KEYS] = {
	[TK_UP] = S_TOUR_KEY_UP, [TK_DOWN] = S_TOUR_KEY_DOWN,
	[TK_LEFT] = S_TOUR_KEY_LEFT, [TK_RIGHT] = S_TOUR_KEY_RIGHT,
	[TK_VOL] = S_TOUR_KEY_VOLUME, [TK_BRIGHT] = S_TOUR_KEY_BRIGHTNESS,
};

/* In English whatever is on, for the command line (--tour-wrong) */
const char *tour_key_name(tour_key k)
{
	if (k < 0 || k >= TK_KEYS) return "";
	return KEY_NAME[k] ? KEY_NAME[k] : T_en(KEY_WORDS[k]);
}

/* As the answer says it, in the language that is on */
static const char *key_said(tour_key k)
{
	return KEY_NAME[k] ? KEY_NAME[k] : T(KEY_WORDS[k]);
}

/* ---- the launcher's buttons ------------------------------------------------ */

/* L2 and R2 are no key of the tutorial's (TK_KEYS): no step asks for them and
 * a press of one is not a wrong one, as it was not before they were the
 * launcher's buttons (the songs on the shelf). Music's note names them and
 * the handheld shows them pressed (tour_hands), and that is all. The Pixel
 * 2's device layer reads L2+R2 together itself, for the battery level. */
static const tour_key KEY_OF[IN_COUNT] = {
	[IN_LEFT] = TK_LEFT, [IN_RIGHT] = TK_RIGHT, [IN_UP] = TK_UP, [IN_DOWN] = TK_DOWN,
	[IN_ACCEPT] = TK_A, [IN_BACK] = TK_B, [IN_X] = TK_X, [IN_Y] = TK_Y,
	[IN_L1] = TK_L1, [IN_R1] = TK_R1,
	[IN_START] = TK_START, [IN_SELECT] = TK_SELECT, [IN_MENU] = TK_MENU,
	[IN_VOLUP] = TK_VOL, [IN_VOLDN] = TK_VOL,
	[IN_BRIGHTUP] = TK_BRIGHT, [IN_BRIGHTDN] = TK_BRIGHT,
	[IN_POWER] = TK_POWER,
	[IN_L2] = TK_KEYS, [IN_R2] = TK_KEYS,
};

tour_key tour_key_of(in_button b)
{
	return b >= 0 && b < IN_COUNT ? KEY_OF[b] : TK_KEYS;
}

const tour_little TOUR_LITTLE[TOUR_LITTLE_N] = {
	{ "Game Boy", 0x7E9B47 }, { "NES", 0xC4443A }, { "SNES", 0x816EBA },
	{ "Game Boy Color", 0xE0B23A }, { "Neo Geo Pocket Color", 0x00AA4F },
};

/* ---- the clocks the drawing reads ----------------------------------------- */

static float curve(tour_curve c, float k)
{
	k = clamp01(k);
	switch (c) {
	case TC_OUT:  return ds_out(k);
	case TC_IN:   return ds_in(k);
	case TC_TURN: return ds_turn(k);
	default:      return k;
	}
}

/* Signed ms from `at` to `now`, so a time still to come is below 0. */
static int since(uint32_t now, uint32_t at) { return (int)(int32_t)(now - at); }

float tour_ease(tour_curve c, uint32_t now, uint32_t at, int ms)
{
	int t = since(now, at);

	if (t < 0) return 0.0f;
	if (ms <= 0) return 1.0f;
	return curve(c, (float)t / (float)ms);
}

int tour_loop_time(const tour *t, int period, uint32_t now)
{
	int ms = since(now, t->step_at) - LOOP_DELAY_MS;

	return ms < 0 || period <= 0 ? 0 : ms % period;
}

/* A part pressed `t0` ms into a loop and let go at `t1`, in **press**. */
static float press_window(int tl, int t0, int t1)
{
	if (tl < t0) return 0.0f;
	if (tl < t1) return curve(TC_OUT, (float)(tl - t0) / DS_MS_PRESS);
	return 1.0f - curve(TC_OUT, (float)(tl - t1) / DS_MS_PRESS);
}

/* When the loop's press acts: the menu at MENU's release where MENU comes on
 * release, at the press on the Brick. */
static int loop_act(const tour *t, const tour_loop *L)
{
	return L->part == TP_MENU && !t->fn_volume ? L->press_at : L->act;
}

tour_loop_at tour_loop_now(const tour *t, uint32_t now)
{
	const tour_loop *L = STEPS[t->step].loop;
	tour_loop_at out = { 0.0f, 0.0f };
	int tl, act;

	if (!L) return out;
	tl = tour_loop_time(t, L->period, now);
	act = loop_act(t, L);
	if (tl >= act) out.a = curve(L->curve, (float)(tl - act) / (float)L->dur);
	if (tl >= L->hold) {
		float k = clamp01((float)(tl - L->hold) / (float)L->back_ms);

		if (L->reverse) out.a = 1.0f - curve(L->back_curve, k);
		else out.back = k;
		if (k >= 1.0f) out.a = out.back = 0.0f;
	}
	return out;
}

float tour_real_a(const tour *t, uint32_t now)
{
	const tour_loop *L = STEPS[t->step].loop;

	if (!L) return 1.0f;
	return t->a0 + (1.0f - t->a0) * tour_ease(L->curve, now, t->done_at, L->dur);
}

float tour_shelf_pos(const tour *t, uint32_t now)
{
	return t->pos_from + ((float)t->pos - t->pos_from)
	       * tour_ease(TC_OUT, now, t->moved_at, DS_MS_MOVE);
}

float tour_focus_pos(const tour *t, uint32_t now)
{
	return tour_shelf_pos(t, now);
}

/* The demonstration's half of a part's press: the step's loop pressing its
 * own button, while the step waits for the player's. */
static float demo_press(const tour *t, tour_part p, uint32_t now)
{
	const step_def *st = &STEPS[t->step];
	int tl;

	if (t->step == TOUR_LANG) return 0.0f;
	if (t->step == TOUR_WELCOME || t->step == TOUR_FINISH) {
		int ms = since(now, t->step_at) - LOOP_A_AT;

		return p == TP_A ? press_window(ms < 0 ? 0 : ms % LOOP_A_MS, 900, 1060) : 0.0f;
	}
	if (t->done || (t->step == TOUR_MOVE && t->moves > 0)) return 0.0f;
	if (st->loop) {
		tl = tour_loop_time(t, st->loop->period, now);
		return p == st->loop->part
		       ? press_window(tl, st->loop->press_at, st->loop->release_at) : 0.0f;
	}
	if (t->step == TOUR_MOVE) {
		tl = tour_loop_time(t, MOVE_LOOP_MS, now);
		return p == TP_RIGHT ? press_window(tl, 600, 780)
		     : p == TP_LEFT  ? press_window(tl, 1700, 1880) : 0.0f;
	}
	if (t->step == TOUR_VOL || t->step == TOUR_BRIGHT) {
		float up;

		tl = tour_loop_time(t, LEVEL_LOOP_MS, now);
		up = press_window(tl, 600, 720);
		if (press_window(tl, 1000, 1120) > up) up = press_window(tl, 1000, 1120);
		if (t->step == TOUR_VOL) return p == TP_VOLUP ? up : 0.0f;
		if (!t->fn_volume) return p == TP_F2 ? up : 0.0f;
		/* MENU held over both steps of the volume key, as the chord is */
		return p == TP_VOLUP ? up : p == TP_MENU ? press_window(tl, 450, 1300) : 0.0f;
	}
	return 0.0f;
}

void tour_hand(tour *t, tour_part p, bool down, uint32_t now)
{
	if (p < 0 || p >= TP_PARTS) return;
	if (down && !t->hand[p].held) {
		t->hand[p].ever = t->hand[p].held = true;
		t->hand[p].down_at = now;
	} else if (!down && t->hand[p].held) {
		t->hand[p].held = false;
		t->hand[p].up_at = now;
	}
}

void tour_hands(tour *t, const bool d[IN_COUNT], bool menu_held, uint32_t now)
{
	const bool fn = t->fn_volume;

	tour_hand(t, TP_UP, d[IN_UP], now);
	tour_hand(t, TP_DOWN, d[IN_DOWN], now);
	tour_hand(t, TP_LEFT, d[IN_LEFT], now);
	tour_hand(t, TP_RIGHT, d[IN_RIGHT], now);
	tour_hand(t, TP_A, d[IN_ACCEPT], now);
	tour_hand(t, TP_B, d[IN_BACK], now);
	tour_hand(t, TP_X, d[IN_X], now);
	tour_hand(t, TP_Y, d[IN_Y], now);
	tour_hand(t, TP_START, d[IN_START], now);
	tour_hand(t, TP_SELECT, d[IN_SELECT], now);
	tour_hand(t, TP_MENU, d[IN_MENU] || menu_held ||
	                      (fn && (d[IN_BRIGHTUP] || d[IN_BRIGHTDN])), now);
	tour_hand(t, TP_VOLUP, d[IN_VOLUP] || (fn && d[IN_BRIGHTUP]), now);
	tour_hand(t, TP_VOLDN, d[IN_VOLDN] || (fn && d[IN_BRIGHTDN]), now);
	tour_hand(t, TP_F1, !fn && d[IN_BRIGHTDN], now);
	tour_hand(t, TP_F2, !fn && d[IN_BRIGHTUP], now);
	tour_hand(t, TP_POWER, d[IN_POWER], now);
	tour_hand(t, TP_L1, d[IN_L1], now);
	tour_hand(t, TP_R1, d[IN_R1], now);
	tour_hand(t, TP_L2, d[IN_L2], now);
	tour_hand(t, TP_R2, d[IN_R2], now);
}

/* The hand's half: in in **press** from when it went down, out in **press**
 * from when it came up, from as far as it had got. */
static float hand_press(const tour *t, tour_part p, uint32_t now)
{
	float reached;

	if (!t->hand[p].ever) return 0.0f;
	reached = tour_ease(TC_OUT, t->hand[p].held ? now : t->hand[p].up_at,
	                    t->hand[p].down_at, DS_MS_PRESS);
	if (t->hand[p].held) return reached;
	return reached * (1.0f - tour_ease(TC_OUT, now, t->hand[p].up_at, DS_MS_PRESS));
}

float tour_part_press(const tour *t, tour_part p, uint32_t now)
{
	float d, h;

	if (p < 0 || p >= TP_PARTS) return 0.0f;
	d = demo_press(t, p, now);
	h = hand_press(t, p, now);
	return d > h ? d : h;
}

bool tour_wrong_on(const tour *t, uint32_t now)
{
	return t->wrong && since(now, t->wrong_at) < DS_MS_HOLD_SHORT + DS_MS_FADE_OUT;
}

/* Twice in tour-pulse, half of it each, dying away to half by its end. */
float tour_wrong_pulse(const tour *t, uint32_t now)
{
	int ms;
	float s;

	if (!t->wrong) return 0.0f;
	ms = since(now, t->wrong_at);
	if (ms < 0 || ms >= DS_MS_TOUR_PULSE) return 0.0f;
	s = sinf(2.0f * (float)M_PI * (float)ms / DS_MS_TOUR_PULSE);
	return (s < 0.0f ? -s : s) * (1.0f - (float)ms / (2.0f * DS_MS_TOUR_PULSE));
}

/* ---- Zyper -------------------------------------------------------------------- */

/* His blinks' dice: any number, the same in every tour, so a shot blinks the
 * same way twice. */
#define ZYPER_SEED 0x7a17u
/* The hello's ears: each tip swings out this far and back in zyper-ears */
#define EAR_SWING  14.0f

static float rise_at(const tour_rise *r, uint32_t now)
{
	return r->from + (r->to - r->from) * tour_ease(r->curve, now, r->t0, r->ms);
}

/* The corner Zyper up (0) in **move** (out), or down (1) in **exit** (in), from
 * wherever he is at `at`. */
static void rise_to(tour *t, float to, uint32_t at)
{
	if (t->rise.to == to) return;
	t->rise = (tour_rise){ rise_at(&t->rise, at), to, at,
	                       to < 0.5f ? DS_MS_MOVE : DS_MS_EXIT, to < 0.5f ? TC_OUT : TC_IN };
}

/* His hello, the welcome's and the finish's: he pops in a **quick** after the
 * screen (at the finish once the corner's has gone, an **exit**), on the
 * welcome with his tongue in until he has, then out in
 * zyper-tongue; at the finish it is out, and he hops every tour-reward from
 * the moment he is in. */
static void hello(tour *t, bool finish, uint32_t now)
{
	/* At the finish the last reward's corner Zyper goes first, in exit
	 * (enter sends him), so one hands over to the other */
	t->hello_at = now + (finish ? DS_MS_EXIT : DS_MS_QUICK);
	t->say_at = t->hello_at;
	zr_press(&t->zy, now);
	if (finish) {
		t->next_hop = t->hello_at + DS_MS_ZYPER_POP;
	} else {
		t->zy.part[ZR_TONGUE] = (zr_tween){ 0.0f, 1.0f, t->hello_at + DS_MS_ZYPER_POP,
		                                    DS_MS_ZYPER_TONGUE, false };
	}
}

/* What happens to him on the clock: the reward's hop and pant once he is up,
 * the corner going down when a wrong press's answer has held, the finish's
 * hops, and the rig's own bookings and blinks; each at the moment it was due,
 * as the rig's are, so a still is the same at any frame rate. */
static void zyper_tick(tour *t, uint32_t now)
{
	if (t->done && !t->cheered && since(now, t->done_at + DS_MS_MOVE) >= 0) {
		zr_works(&t->zy, t->done_at + DS_MS_MOVE);
		t->cheered = true;
	}
	if (t->wrong && !t->done && t->rise.to == 0.0f &&
	    since(now, t->wrong_at + DS_MS_HOLD_SHORT) >= 0)
		rise_to(t, 1.0f, t->wrong_at + DS_MS_HOLD_SHORT);
	if (t->step == TOUR_FINISH)
		while (since(now, t->next_hop) >= 0) {
			zr_works(&t->zy, t->next_hop);
			t->next_hop += DS_MS_TOUR_REWARD;
		}
	zr_tick(&t->zy, now);
}

tour_zyper_at tour_zyper_now(const tour *t, uint32_t now)
{
	tour_zyper_at z;

	z.pose = zr_pose_at(&t->zy, now);
	z.scale = 1.0f;
	z.drop = rise_at(&t->rise, now);
	/* In the power step the stage's Zyper is the one until its press: the
	 * corner only lets the last step's go down. At the welcome there is no
	 * corner yet, and at the finish only the last reward's, going */
	z.corner = t->step != TOUR_WELCOME && t->step != TOUR_LANG && z.drop < 1.0f &&
	           !(t->step == TOUR_POWER && !t->done && t->rise.to < 0.5f);
	z.hello = false;
	if (t->step == TOUR_LANG || t->step == TOUR_WELCOME || t->step == TOUR_FINISH) {
		const int ms = since(now, t->hello_at);

		z.hello = ms > 0;
		if (z.hello) z.corner = false;
		z.scale = ms <= 0 ? 0.0f : zr_overshoot((float)ms / DS_MS_ZYPER_POP);
		z.pose.ears = EAR_SWING * sinf((float)M_PI *
		              clamp01((float)(ms - DS_MS_ZYPER_POP) / DS_MS_ZYPER_EARS));
	}
	return z;
}

/* ---- the steps' model ----------------------------------------------------- */

static void enter(tour *t, tour_step s, uint32_t now)
{
	if (t->done) {
		t->had_reward = true;
		t->last_step = t->step;
		t->last_at = t->done_at;
	} else {
		t->had_reward = false;
	}
	/* The window he was up for is over, from wherever he is */
	rise_to(t, 1.0f, now);
	t->cheered = false;
	/* The welcome after the language step: Zyper is there already, and only
	 * the welcome's words come in */
	if (s == TOUR_WELCOME && t->from_lang) t->say_at = now;
	else if (s == TOUR_LANG || s == TOUR_WELCOME || s == TOUR_FINISH) hello(t, s == TOUR_FINISH, now);
	t->from_lang = false;
	t->step = s;
	t->step_at = now;
	t->done = false;
	t->done_at = 0;
	t->a0 = 0.0f;
	t->moves = 0;
	t->pos = 0;
	t->pos_from = 0.0f;
	t->moved_at = now;
	t->wrong = false;
	/* A choice starts on the setting as it is */
	if (tour_choice_of(s) >= 0) {
		const int c = tour_choice_of(s);

		t->pick[c] = t->kept[c];
		t->pos = t->pick[c];
		t->pos_from = (float)t->pos;
	}
}

/* The step after `s`: the menu music's only on a card that has some */
static tour_step next_step(const tour *t, tour_step s)
{
	s = (tour_step)(s + 1);
	if (s == TOUR_MUSIC && !t->music) s = TOUR_FINISH;
	return s;
}

void tour_choices_from(tour *t, const int at[TOUR_CHOICES])
{
	int c;

	for (c = 0; c < TOUR_CHOICES; c++) {
		t->kept[c] = t->pick[c] = at[c] >= 0 && at[c] < CHOICES[c].n ? at[c] : 0;
		if (tour_choice_of(t->step) == c) {
			t->pos = t->pick[c];
			t->pos_from = (float)t->pos;
		}
	}
}

void tour_set_music(tour *t, bool has)
{
	t->music = has;
}

int tour_segments(const tour *t)
{
	return TOUR_LESSONS + TOUR_CHOICES - (t->music ? 0 : 1);
}

int tour_segment(const tour *t)
{
	if (t->step == TOUR_LANG || t->step == TOUR_WELCOME) return -1;
	if (t->step == TOUR_FINISH) return tour_segments(t);
	if (tour_choice_of(t->step) >= 0) return TOUR_LESSONS + tour_choice_of(t->step);
	return t->step - TOUR_FIRST_LESSON;
}

void tour_start(tour *t, uint32_t now, bool fn_volume)
{
	memset(t, 0, sizeof *t);
	t->fn_volume = fn_volume;
	zr_init(&t->zy, now, ZYPER_SEED);
	t->rise = (tour_rise){ 1.0f, 1.0f, now, 0, TC_OUT };
	enter(t, TOUR_LANG, now);
}

/* ---- the language ------------------------------------------------------------ */

void tour_lang_from(tour *t, int lang)
{
	if (lang < 0 || lang >= TOUR_LANGS) lang = 0;
	t->lang_start = t->lang_pick = t->lang_kept = lang;
}

int tour_lang_focus(const tour *t, uint32_t now)
{
	int n;

	if (t->lang_touched || t->step != TOUR_LANG) return t->lang_pick;
	n = since(now, t->step_at) / TOUR_LANG_STEP_MS;
	return (t->lang_start + (n > 0 ? n : 0)) % TOUR_LANGS;
}

int tour_lang_kept(const tour *t)
{
	return t->lang_kept;
}

/* Whether a press is the one the step waits for. */
static bool answers(const tour *t, tour_key k)
{
	switch (t->step) {
	case TOUR_MOVE:   return k == TK_UP || k == TK_DOWN || k == TK_LEFT || k == TK_RIGHT ||
	                         k == TK_L1 || k == TK_R1;
	case TOUR_A:      return k == TK_A;
	case TOUR_B:      return k == TK_B;
	case TOUR_Y:      return k == TK_Y;
	case TOUR_X:      return k == TK_X;
	case TOUR_MENU:   return k == TK_MENU;
	case TOUR_SELECT: return k == TK_SELECT;
	case TOUR_VOL:    return k == TK_VOL;
	case TOUR_BRIGHT: return k == TK_BRIGHT;
	case TOUR_POWER:  return k == TK_POWER;
	default:          return false;
	}
}

/* The press landed: the reward, with Zyper up from the bottom edge, from
 * wherever he is, and his cocked head let go of if he had one. */
static int landed(tour *t, uint32_t now)
{
	t->a0 = STEPS[t->step].loop ? tour_loop_now(t, now).a : 0.0f;
	t->done = true;
	t->done_at = now;
	t->wrong = false;
	t->cheered = false;
	zr_press(&t->zy, now);
	rise_to(t, 0.0f, now);
	return DS_SOUND_REWARD;
}

/* Zyper's answer to a wrong press: which button that was, and which to try.
 * In the brightness step, a volume key alone says to hold MENU too, and MENU
 * alone how the chord goes. */
static int missed(tour *t, tour_key k, uint32_t now)
{
	const tour_info *st = tour_step_info(t->step);
	const char *want = t->fn_volume && st->want_fn ? st->want_fn : st->want;

	/* On the Brick a volume key there is "That's volume. Try F1 or F2.",
	 * the answer every other wrong press gets */
	if (t->step == TOUR_BRIGHT && k == TK_VOL && t->fn_volume)
		snprintf(t->hint, sizeof t->hint, "%s", T(S_TOUR_WRONG_HOLD_MENU));
	else if (t->step == TOUR_BRIGHT && k == TK_MENU && t->fn_volume)
		snprintf(t->hint, sizeof t->hint, "%s", T(S_TOUR_WRONG_CHORD));
	else
		snprintf(t->hint, sizeof t->hint, T(S_TOUR_WRONG), key_said(k), want);
	t->wrong = true;
	t->wrong_at = now;
	/* His answer to a failure, the menu's (section 10): the tongue in and
	 * the head cocked, held, then settled. Up from the bottom edge for it
	 * in a step, but in the power step, whose Zyper is the stage's: there
	 * the bubble says it alone. */
	zr_press(&t->zy, now);
	zr_fails(&t->zy, now);
	if (t->step != TOUR_WELCOME && t->step != TOUR_POWER) rise_to(t, 0.0f, now);
	return DS_SOUND_DENY;
}

int tour_press(tour *t, tour_key k, uint32_t now)
{
	int in_step = since(now, t->step_at);

	if (k < 0 || k >= TK_KEYS || t->over) return TOUR_QUIET;

	/* Any press stops the languages going round, where they are (my
	 * note: "until the user has any input") */
	if (t->step == TOUR_LANG && !t->lang_touched) {
		t->lang_pick = tour_lang_focus(t, now);
		t->lang_touched = true;
	}

	/* The levels' keys keep working in every step, as on every screen; the
	 * caller has turned the level already. */
	if (k == TK_VOL || k == TK_BRIGHT) {
		if (t->done || in_step < DEAF_MS) return TOUR_QUIET;
		if (answers(t, k)) return landed(t, now);
		if (t->step == TOUR_BRIGHT && k == TK_VOL) return missed(t, k, now);
		return TOUR_QUIET;
	}
	/* Anywhere but its own step POWER is the caller's, and turns the
	 * handheld off: tour_takes_power. */
	if (k == TK_POWER) {
		if (!tour_takes_power(t) || in_step < DEAF_MS) return TOUR_QUIET;
		return landed(t, now);
	}
	if (k == TK_START) {
		if (in_step < DEAF_MS) return TOUR_QUIET;
		/* Skipped from the language step: the one the player moved to, or
		 * the one that was on; going round alone chose nothing */
		if (t->step == TOUR_LANG) t->lang_kept = t->lang_moved ? t->lang_pick : t->lang_start;
		t->skipped = t->step != TOUR_FINISH;
		t->over = true;
		t->end_at = now;
		return DS_SOUND_BACK;
	}
	if (t->step == TOUR_LANG) {
		if (in_step < DEAF_MS) return TOUR_QUIET;
		if (k == TK_UP || k == TK_DOWN || k == TK_LEFT || k == TK_RIGHT) {
			const int to = t->lang_pick + (k == TK_UP || k == TK_LEFT ? -1 : 1);

			t->pos_from = (float)t->lang_pick;
			t->lang_pick = (to + TOUR_LANGS) % TOUR_LANGS;
			t->moved_at = now;
			t->lang_moved = true;
			return t->lang_pick != to ? DS_SOUND_EDGE : DS_SOUND_MOVE;
		}
		if (k != TK_A) return TOUR_QUIET;
		t->lang_kept = t->lang_pick;
		t->from_lang = true;
		enter(t, TOUR_WELCOME, now);
		return DS_SOUND_ACCEPT;
	}
	if (t->step == TOUR_WELCOME) {
		if (in_step < DEAF_MS) return TOUR_QUIET;
		if (k != TK_A) return missed(t, k, now);
		enter(t, TOUR_MOVE, now);
		return DS_SOUND_ACCEPT;
	}
	if (t->step == TOUR_FINISH) {
		if (k != TK_A || in_step < FINISH_DEAF_MS) return TOUR_QUIET;
		t->over = true;
		t->end_at = now;
		return DS_SOUND_ACCEPT;
	}
	if (t->done || in_step < DEAF_MS) return TOUR_QUIET;
	/* A choice: LEFT and RIGHT look, round its choices, and A keeps the one
	 * looked at; the caller applies and saves them. Anything else is a
	 * wrong press. */
	if (tour_choice_of(t->step) >= 0) {
		const int c = tour_choice_of(t->step), n = CHOICES[c].n;

		if (k == TK_LEFT || k == TK_RIGHT) {
			const int to = t->pick[c] + (k == TK_LEFT ? -1 : 1);
			const int at = (to + n) % n;

			t->pos_from = tour_focus_pos(t, now);
			t->pick[c] = t->pos = at;
			t->moved_at = now;
			return at != to ? DS_SOUND_EDGE : DS_SOUND_MOVE;
		}
		if (k != TK_A) return missed(t, k, now);
		t->kept[c] = t->pick[c];
		return landed(t, now);
	}
	if (!answers(t, k)) return missed(t, k, now);
	if (t->step == TOUR_MOVE) {
		/* L1/R1 jump as they do on the home, 3 systems (HOME_JUMP) */
		const int jump = k == TK_L1 || k == TK_R1 ? 3 : 1;
		const int d = k == TK_LEFT || k == TK_UP || k == TK_L1 ? -1 : 1;
		int to = t->pos + d * jump;
		bool edge;

		/* The little shelf follows, from wherever it is, as far as it has
		 * cards: it has five, so a jump stops at its end, and goes nowhere
		 * from there. */
		if (to < -2) to = -2;
		if (to > 2) to = 2;
		edge = to == t->pos;
		t->pos_from = tour_shelf_pos(t, now);
		t->pos = to;
		t->moved_at = now;
		if (++t->moves < MOVES_WANTED) return edge ? DS_SOUND_EDGE : DS_SOUND_MOVE;
	}
	return landed(t, now);
}

int tour_buttons(tour *t, const bool pressed[IN_COUNT], uint32_t now, int sounds[IN_COUNT])
{
	int b, n = 0;

	for (b = 0; b < IN_COUNT && !t->over; b++) {
		int snd;

		if (b == IN_POWER || !pressed[b]) continue;
		snd = tour_press(t, tour_key_of((in_button)b), now);
		if (snd != TOUR_QUIET) sounds[n++] = snd;
	}
	return n;
}

void tour_tick(tour *t, uint32_t now)
{
	if (t->over) return;
	zyper_tick(t, now);
	if (t->done && since(now, t->done_at) >= DS_MS_TOUR_REWARD && t->step < TOUR_FINISH)
		enter(t, next_step(t, t->step), now);
}

bool tour_takes_power(const tour *t)
{
	return !t->over && t->step == TOUR_POWER && !t->done;
}

/* ---- stills ----------------------------------------------------------------- */

void tour_pose(tour *t, tour_step step, uint32_t now, int ms, int done_ms,
               bool fn_volume)
{
	const uint32_t at = now - (uint32_t)ms;

	if (step == TOUR_FINISH) {
		/* The finish inherits the last choice's corner Zyper. Replay that
		 * reward through the model so its exit is present in a shot too;
		 * entering the finish directly would start with an empty corner. */
		const uint32_t reward_at = at - DS_MS_TOUR_REWARD;

		tour_start(t, reward_at, fn_volume);
		enter(t, t->music ? TOUR_MUSIC : TOUR_SOUNDS, reward_at);
		landed(t, reward_at);
		tour_tick(t, at);
	} else {
		tour_start(t, at, fn_volume);
		enter(t, step, at);
	}
	if (done_ms >= 0) {
		/* Landed where the loop was then, as a press does */
		landed(t, now - (uint32_t)done_ms);
		if (step == TOUR_MOVE) {
			t->moves = MOVES_WANTED;
			t->pos = 1;
			t->pos_from = 1.0f;
		}
	}
	zyper_tick(t, now);
}

void tour_pose_wrong(tour *t, tour_key k, uint32_t now, int ms)
{
	missed(t, k, now - (uint32_t)ms);
	zyper_tick(t, now);
}

void tour_pose_pick(tour *t, int pick, uint32_t now, int ms)
{
	const int c = tour_choice_of(t->step);

	/* The language step: the cursor moved there, by the player */
	if (t->step == TOUR_LANG && pick >= 0 && pick < TOUR_LANGS) {
		t->pos_from = (float)t->lang_pick;
		t->lang_pick = pick;
		t->lang_touched = t->lang_moved = true;
		t->moved_at = now - (uint32_t)ms;
		return;
	}

	if (c < 0 || pick < 0 || pick >= CHOICES[c].n) return;
	t->pos_from = (float)t->pick[c];
	t->pick[c] = t->pos = pick;
	t->moved_at = now - (uint32_t)ms;
}

/* ---- the setting -------------------------------------------------------------- */

bool tour_wanted(db *dev)
{
	return dev && !db_get_int(dev, TOUR_DONE_KEY, 0);
}

bool tour_seen(const tour *t)
{
	return t->over || t->step > TOUR_POWER || (t->step == TOUR_POWER && t->done);
}

void tour_save(db *dev, const tour *t)
{
	if (dev && tour_seen(t) && !db_set_int(dev, TOUR_DONE_KEY, 1))
		fprintf(stderr, "tour: could not remember that it was seen\n");
}
