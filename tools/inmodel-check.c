/* The input model (src/inmodel.h, src/inengine.h, src/device/devinput.h),
 * for EVERY device profile, each through its own codes and axes, as its
 * handheld sends them:
 *
 * - the logical buttons' one table: each row where its id is, every name
 *   and word once, every button something to the tutorial and a key on the
 *   handheld's drawing;
 * - each profile's table: every code once, every button it prints reached
 *   by a code or a stick and every button it sends printed, its sticks;
 * - every code pressed and let go: what the hand holds, and the one press
 *   it makes (MENU as it is let go, the Pixel 2's L2 and R2 as taps);
 * - MENU's chords on every key that can be one (the screenshot's, the
 *   lock's, swapped), and the volume keys under MENU;
 * - the hotkeys' two capture screens: MENU and every key it can take,
 *   SELECT and every key it can hold, caught as that key;
 * - both sticks: still at rest and under their dead middle, each direction
 *   its own button, the right one never the d-pad;
 * - the tutorial: every lesson, every code, every stick direction and every
 *   chord, exactly the right ones passing, every wrong one answered with
 *   the button's own name (FN on the R36S), and the glyph lit for a press
 *   the button the answer saw.
 *
 * The R36S's FN+L2 that was "That's L2. Hold FN too." and its Hotkeys
 * capture that never caught FN+L2 or FN+R2, the Brick's MENU that skipped
 * the chord rule (#496) and the Pixel 2's capture glyph (#464) are each a
 * case here. No SDL. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/design.h"
#include "../src/hotkeys.h"
#include "../src/i18n.h"
#include "../src/inengine.h"
#include "../src/inmodel.h"
#include "../src/menukeys.h"
#include "../src/tour.h"

static int fails, checks;

static void ck(bool ok, const char *fmt, ...)
{
	va_list ap;

	checks++;
	if (ok) return;
	fails++;
	printf("  FAIL ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

/* ---- the launcher's side, as platform.c and main.c have it ---------------- */

/* in_state, as platform.c's sink fills it */
typedef struct {
	bool down[IN_COUNT], pressed[IN_COUNT], held[IN_COUNT];
} sim;

static void s_set(void *c, in_button b, bool d)
{
	sim *s = c;

	if (d && !s->down[b]) s->pressed[b] = true;
	s->down[b] = d;
}
static void s_hold(void *c, in_button b, bool d) { ((sim *)c)->down[b] = d; }
static void s_press(void *c, in_button b) { ((sim *)c)->pressed[b] = true; }
static void s_hand(void *c, in_button b, bool d) { ((sim *)c)->held[b] = d; }

/* MENU's chords as src/menukeys_screen.c takes and runs them */
static mk_table g_mk;
static bool g_capture;
static int g_caught, g_locks, g_shots, g_battery;

static bool take(in_button b)
{
	const int k = hk_key_of(b);

	if (g_capture) {
		if (!mk_key_ok(k)) return false;
		g_caught = k;
		return true;
	}
	return mk_act_on(&g_mk, k) >= 0;
}

static void run(in_button b)
{
	const int act = mk_act_on(&g_mk, hk_key_of(b));

	if (g_capture) return;
	if (act == MK_SHOT) g_shots++;
	else if (act == MK_LOCK) g_locks++;
}

static void battery(void) { g_battery++; }

/* One handheld: the engine, its in_state, and the tutorial when one runs */
typedef struct {
	const device_profile *p;
	inengine e;
	sim      st;
	ie_sink  sk;
	tour     t;
	bool     touring;
	uint32_t now;
	int      locks_seen;
	/* What the answer saw: each key with the button it came from */
	tour_key saw[32];
	in_button saw_b[32];
	int      nsaw;
	bool     lit_prev[TP_PARTS];
	char     glyph_fail[160];
	int      presses[IN_COUNT];   /* each button's presses, all polls */
} rig;

static const int AX_MIN = -1800, AX_MAX = 1800, AX_FLAT = 32;

static void rig_init(rig *r, const device_profile *p)
{
	int i;

	memset(r, 0, sizeof *r);
	r->p = p;
	r->e.take = take;
	r->e.battery = battery;
	ie_init(&r->e, p);
	r->sk = (ie_sink){ &r->st, s_set, s_hold, s_press, s_hand };
	r->now = 100000u;
	g_capture = false;
	g_caught = -1;
	g_locks = g_shots = g_battery = 0;
	mk_default(&g_mk);
	/* Each stick at rest, as the kernel gives its range */
	for (i = 0; i < DEV_STICKS; i++) {
		const dev_stick *d = &p->input.stick[i];

		if (!d->present) continue;
		ie_axis(&r->e, d->axis_x, 0, AX_MIN, AX_MAX, AX_FLAT);
		ie_axis(&r->e, d->axis_y, 0, AX_MIN, AX_MAX, AX_FLAT);
	}
	ie_sync(&r->e, &r->sk);
}

static void tour_on(rig *r, tour_step s)
{
	tour_pose(&r->t, s, r->now, 2000, -1, &r->p->chords, NULL);
	r->touring = true;
	r->locks_seen = g_locks;
	r->nsaw = 0;
	r->glyph_fail[0] = '\0';
	memset(r->lit_prev, 0, sizeof r->lit_prev);
}

static void poll_begin(rig *r)
{
	memset(r->st.pressed, 0, sizeof r->st.pressed);
	ie_poll_begin(&r->e, &r->sk);
}

static void saw(rig *r, tour_key k, in_button b)
{
	if (r->nsaw < 32) {
		r->saw[r->nsaw] = k;
		r->saw_b[r->nsaw++] = b;
	}
}

/* The part `b` lights was lit this poll or the one before (MENU and the
 * Pixel 2's L2 are pressed as they are let go) */
static bool lit_for(const rig *r, in_button b, const bool lit[TP_PARTS])
{
	const tour_part p = tour_part_of(b);

	return p == TP_PARTS || lit[p] || r->lit_prev[p];
}

static void glyph_fail(rig *r, const char *fmt, const char *a, const char *b)
{
	if (!r->glyph_fail[0]) snprintf(r->glyph_fail, sizeof r->glyph_fail, fmt, a, b);
}

/* The poll's end, as plat_input_poll's and the tutorial's frame: the chord
 * carried out, then the tutorial given the presses and the hand, the glyphs
 * held to what the answer saw */
static void poll_end(rig *r)
{
	in_button due;
	bool lit[TP_PARTS] = { false };
	int b, n, snd[IN_COUNT], p;

	ie_sync(&r->e, &r->sk);
	if ((due = ie_due(&r->e)) != IN_NONE) run(due);
	for (b = 0; b < IN_COUNT; b++)
		if (r->st.pressed[b]) r->presses[b]++;
	if (r->touring) {
		tour_hands(&r->t, r->st.held, r->now);
		for (p = 0; p < TP_PARTS; p++) lit[p] = r->t.hand[p].held;
		/* What the hand lights is what it holds, part for part */
		for (b = 0; b < IN_COUNT; b++)
			if (r->st.held[b] && tour_part_of((in_button)b) != TP_PARTS &&
			    !lit[tour_part_of((in_button)b)])
				glyph_fail(r, "%s held, its glyph not lit%s", in_info_of((in_button)b)->name, "");
		if (g_locks != r->locks_seen) {
			r->locks_seen = g_locks;
			saw(r, TK_LOCK, IN_MENU);
			if (!lit_for(r, IN_MENU, lit) || !lit_for(r, tour_menu_key(1), lit))
				glyph_fail(r, "the lock's chord answered with %s or %s not lit", "MENU",
				           in_info_of(tour_menu_key(1))->name);
			tour_press(&r->t, TK_LOCK, r->now);
		}
		if (r->st.pressed[IN_POWER] && tour_takes_power(&r->t)) {
			saw(r, TK_POWER, IN_POWER);
			tour_press(&r->t, TK_POWER, r->now);
		}
		tour_hold(&r->t, r->st.down[IN_SELECT], r->st.down[IN_START], r->now);
		for (b = 0; b < IN_COUNT; b++) {
			if (b == IN_POWER || !r->st.pressed[b]) continue;
			saw(r, tour_key_of((in_button)b), (in_button)b);
			/* The glyph lit is the button the answer sees: its own part,
			 * and never MENU's chord shown while the answer sees its
			 * button alone (the R36S's FN+L2) */
			{
				in_button made[2];
				int m, nm = ie_made_of(r->p, (in_button)b, made);

				for (m = 0; m < nm; m++)
					if (!lit_for(r, made[m], lit))
						glyph_fail(r, "%s answered with %s's glyph not lit", in_info_of((in_button)b)->name,
						           in_info_of(made[m])->name);
			}
			if (b != IN_MENU && r->st.held[IN_MENU] && take((in_button)b))
				glyph_fail(r, "MENU+%s lit, the answer saw %s alone", in_info_of((in_button)b)->name,
				           in_info_of((in_button)b)->name);
		}
		n = tour_buttons(&r->t, r->st.pressed, r->now, snd);
		(void)n;
		memcpy(r->lit_prev, lit, sizeof lit);
	}
	r->now += 40;
}

/* One poll with one key's edge in it */
static bool key(rig *r, int code, int value)
{
	bool known;

	poll_begin(r);
	known = ie_key(&r->e, &r->sk, code, value);
	poll_end(r);
	return known;
}

/* A poll with nothing in it */
static void idle(rig *r)
{
	poll_begin(r);
	poll_end(r);
}

/* Stick `i` at (fx, fy) of its way, -1 to 1 */
static void stick(rig *r, int i, float fx, float fy)
{
	const dev_stick *d = &r->p->input.stick[i];

	poll_begin(r);
	ie_abs(&r->e, d->axis_x, (int)(fx * AX_MAX));
	ie_abs(&r->e, d->axis_y, (int)(fy * AX_MAX));
	poll_end(r);
}

/* The code of `b` on `p`, -1 for none */
static int code_of(const device_profile *p, in_button b)
{
	int i;

	for (i = 0; i < p->input.n_keys; i++)
		if (p->input.keys[i].b == b) return p->input.keys[i].code;
	return -1;
}

static int count_true(const bool *v, int n)
{
	int i, c = 0;

	for (i = 0; i < n; i++) c += v[i];
	return c;
}

/* ---- the model's table ---------------------------------------------------- */

static void model(void)
{
	int b, c;

	printf("the logical buttons:\n");
	for (b = 0; b < IN_COUNT; b++) {
		const in_info *in = in_info_of((in_button)b);

		ck(in && in->id == (in_button)b, "row %d is button %d", b, b);
		ck(in_by_name(in->name) == (in_button)b, "%s is found by its name", in->name);
		ck(in_by_word(in->word) == (in_button)b, "%s is found by its word %s", in->name, in->word);
		ck(in->key < TD_KEYS, "%s is a key of the handheld's drawing", in->name);
		ck(tour_key_of((in_button)b) != TK_KEYS, "%s is something to the tutorial", in->name);
		for (c = 0; c < b; c++) {
			ck(strcmp(in->name, in_info_of((in_button)c)->name) != 0, "%s named once", in->name);
			ck(strcmp(in->word, in_info_of((in_button)c)->word) != 0, "%s's word once", in->name);
		}
	}
	ck(!in_info_of(IN_NONE) && !in_info_of(IN_COUNT), "no button is no row");
	for (b = 0; b < HK_KEYS; b++) {
		ck(hk_button(b) != IN_NONE && hk_key_of(hk_button(b)) == b, "hotkey key %d is one button", b);
		ck(!strcmp(hk_key_name(b), in_info_of(hk_button(b))->name) &&
		   !strcmp(hk_key_map(b), in_info_of(hk_button(b))->word),
		   "hotkey key %s by the button's own name and word", hk_key_name(b));
	}
	ck(hk_key_of(IN_MENU) == HK_NONE && hk_key_of(IN_UP) == HK_NONE && hk_key_of(IN_RS_UP) == HK_NONE,
	   "MENU, the d-pad and the sticks are no hotkey's key");
}

/* ---- a profile's table ------------------------------------------------------ */

static bool printed(const device_profile *p, in_button b) { return dev_button_of(p, b) != NULL; }

static bool reached(const device_profile *p, in_button b)
{
	int i;

	if (code_of(p, b) >= 0) return true;
	for (i = 0; i < DEV_STICKS; i++)
		if (p->input.stick[i].present && b >= p->input.stick[i].first &&
		    b < p->input.stick[i].first + 4)
			return true;
	return false;
}

static void table(const device_profile *p)
{
	int i, j, b;

	for (i = 0; i < p->input.n_keys; i++) {
		const dev_code *k = &p->input.keys[i];

		ck(in_info_of(k->b) != NULL, "%s: code 0x%x is a button", p->id, k->code);
		ck(in_info_of(k->b) && in_info_of(k->b)->kind != IK_STICK,
		   "%s: code 0x%x is no stick's push", p->id, k->code);
		for (j = 0; j < i; j++) {
			ck(p->input.keys[j].code != k->code, "%s: code 0x%x once", p->id, k->code);
			ck(p->input.keys[j].b != k->b, "%s: %s from one code", p->id, in_info_of(k->b)->name);
		}
		ck(printed(p, k->b), "%s: %s, which it sends, is printed", p->id, in_info_of(k->b)->name);
	}
	for (i = 0; i < p->n_buttons; i++)
		ck(reached(p, p->buttons[i].id), "%s: %s, printed, is sent", p->id, p->buttons[i].label);
	for (b = 0; b < IN_COUNT; b++)
		if (in_info_of((in_button)b)->kind != IK_STICK && !printed(p, (in_button)b))
			ck(!reached(p, (in_button)b), "%s: %s is not printed and never sent", p->id,
			   in_info_of((in_button)b)->name);
	for (i = 0; i < DEV_STICKS; i++) {
		const dev_stick *d = &p->input.stick[i];

		if (!d->present) continue;
		ck(d->first == (i == DEV_STICK_L ? IN_LEFT : IN_RS_LEFT),
		   "%s: the %s stick is %s", p->id, i ? "right" : "left", i ? "its own four" : "the d-pad");
		ck(d->axis_x != d->axis_y && d->dead >= 0, "%s: stick %d's two axes", p->id, i);
		for (j = 0; j < i; j++)
			ck(!p->input.stick[j].present ||
			   (p->input.stick[j].axis_x != d->axis_x && p->input.stick[j].axis_y != d->axis_y),
			   "%s: each axis one stick's", p->id);
	}
	if (p->chords.stick_dpad)
		ck(p->input.stick[DEV_STICK_L].present && p->input.stick[DEV_STICK_L].first == IN_LEFT &&
		   printed(p, IN_L3), "%s: the pages' left stick is its d-pad, by L3", p->id);
}

/* ---- every code through the engine ------------------------------------------ */

static void codes(const device_profile *p)
{
	int i, b;
	rig r;

	for (i = 0; i < p->input.n_keys; i++) {
		const in_button k = p->input.keys[i].b;
		const char *name = in_info_of(k)->name;
		const bool tap = (k == IN_L2 || k == IN_R2) && p->chords.lr2_taps;
		int down_presses;

		rig_init(&r, p);
		key(&r, p->input.keys[i].code, 1);
		ck(r.st.held[k] && count_true(r.st.held, IN_COUNT) == 1, "%s: %s held is %s alone in the hand",
		   p->id, name, name);
		down_presses = count_true(r.st.pressed, IN_COUNT);
		key(&r, p->input.keys[i].code, 2);
		ck(!r.st.pressed[k], "%s: %s's repeat is no press", p->id, name);
		key(&r, p->input.keys[i].code, 0);
		ck(!r.st.held[k], "%s: %s let go leaves the hand", p->id, name);
		idle(&r);
		for (b = 0; b < IN_COUNT; b++)
			ck(r.presses[b] == (b == (int)k), "%s: %s makes %s %d press(es)", p->id, name,
			   in_info_of((in_button)b)->name, r.presses[b]);
		if (k == IN_MENU || tap)
			ck(down_presses == 0, "%s: %s is pressed as it is let go", p->id, name);
		else
			ck(down_presses == 1, "%s: %s is pressed as it goes down", p->id, name);
		ck(!r.st.down[k], "%s: %s is up at the end", p->id, name);
	}
	ck(!ie_key(&r.e, &r.sk, 0x2ff, 1) && ie_code(p, 0x2ff) == IN_NONE, "%s: a code it has none for is nothing",
	   p->id);

	/* MENU and a volume key: brightness where it has no keys for it */
	rig_init(&r, p);
	key(&r, code_of(p, IN_MENU), 1);
	key(&r, code_of(p, IN_VOLUP), 1);
	key(&r, code_of(p, IN_VOLUP), 0);
	key(&r, code_of(p, IN_MENU), 0);
	idle(&r);
	if (p->chords.bright == DEV_BRIGHT_MENU_VOL)
		ck(r.presses[IN_BRIGHTUP] == 1 && !r.presses[IN_VOLUP] && !r.presses[IN_MENU],
		   "%s: MENU+VOL+ is brightness up, and MENU opens nothing", p->id);
	else
		ck(r.presses[IN_VOLUP] == 1 && !r.presses[IN_BRIGHTUP] && r.presses[IN_MENU] == 1,
		   "%s: MENU+VOL+ is the volume, and MENU still opens", p->id);
	ck(r.st.held[IN_MENU] == false && r.st.held[IN_VOLUP] == false, "%s: and the hand is empty", p->id);

	/* L2 and R2 together: the battery's lights where it has them */
	rig_init(&r, p);
	key(&r, code_of(p, IN_L2), 1);
	key(&r, code_of(p, IN_R2), 1);
	ck(r.st.held[IN_L2] && r.st.held[IN_R2], "%s: L2 and R2 held together are both in the hand", p->id);
	key(&r, code_of(p, IN_R2), 0);
	key(&r, code_of(p, IN_L2), 0);
	idle(&r);
	if (p->chords.battery_chord)
		ck(g_battery == 1 && !r.presses[IN_L2] && !r.presses[IN_R2],
		   "%s: L2+R2 shows the battery, and is no song", p->id);
	else
		ck(g_battery == 0 && r.presses[IN_L2] == 1 && r.presses[IN_R2] == 1,
		   "%s: L2+R2 is each its own press", p->id);
}

/* ---- MENU's chords, and the capture screens ---------------------------------- */

static void chord(rig *r, in_button k)
{
	key(r, code_of(r->p, IN_MENU), 1);
	key(r, code_of(r->p, k), 1);
	key(r, code_of(r->p, k), 0);
	key(r, code_of(r->p, IN_MENU), 0);
	idle(r);
}

static void chords(const device_profile *p)
{
	int k;
	rig r;

	for (k = 0; k < HK_KEYS; k++) {
		const in_button b = hk_button(k);
		const int act = mk_act_on(&g_mk, k);

		rig_init(&r, p);
		chord(&r, b);
		if (mk_act_on(&g_mk, k) >= 0) {
			ck(!r.presses[b] && !r.presses[IN_MENU], "%s: MENU+%s is the chord alone", p->id, hk_key_name(k));
			ck(act == MK_SHOT ? g_shots == 1 && !g_locks : g_locks == 1 && !g_shots,
			   "%s: MENU+%s does its job once", p->id, hk_key_name(k));
		} else {
			ck(r.presses[b] == 1 && r.presses[IN_MENU] == 1 && !g_shots && !g_locks,
			   "%s: MENU+%s, no chord, is %s, then MENU", p->id, hk_key_name(k), hk_key_name(k));
		}
	}
	/* The lock on another key (MENU > System > Hotkeys) */
	rig_init(&r, p);
	mk_swap(&g_mk, MK_LOCK, HK_R2);
	chord(&r, IN_R2);
	ck(g_locks == 1 && !r.presses[IN_R2] && !r.presses[IN_MENU], "%s: the lock moved to R2 is MENU+R2", p->id);
	chord(&r, IN_L2);
	ck(r.presses[IN_L2] == 1 && g_locks == 1, "%s: and MENU+L2 then is L2 and MENU", p->id);
}

static void capture(const device_profile *p)
{
	int k;
	rig r;

	/* MENU's: every key it can take, caught as that key, and nothing else
	 * done with it */
	for (k = 0; k < HK_KEYS; k++) {
		const in_button b = hk_button(k);

		if (!mk_key_ok(k)) continue;
		rig_init(&r, p);
		g_capture = true;
		chord(&r, b);
		ck(g_caught == k && !r.presses[b] && !r.presses[IN_MENU] && !g_shots && !g_locks,
		   "%s: the capture of MENU's chords catches MENU+%s", p->id, hk_key_name(k));
		g_capture = false;
	}
	/* A game's: SELECT held, and every key it can hold pressed under it */
	for (k = 0; k < HK_KEYS; k++) {
		const in_button b = hk_button(k);
		bool caught = false;
		int step;
		const int codes[4][2] = { { code_of(p, IN_SELECT), 1 }, { code_of(p, b), 1 },
		                          { code_of(p, b), 0 }, { code_of(p, IN_SELECT), 0 } };

		rig_init(&r, p);
		for (step = 0; step < 4; step++) {
			key(&r, codes[step][0], codes[step][1]);
			if (r.st.pressed[b] && r.st.down[IN_SELECT] && r.st.held[IN_SELECT]) caught = true;
		}
		ck(caught, "%s: the capture of a game's hotkeys catches SELECT+%s", p->id, hk_key_name(k));
	}
}

/* ---- the sticks --------------------------------------------------------------- */

static void sticks(const device_profile *p)
{
	static const float DIR[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
	int i, d, b;
	rig r;

	for (i = 0; i < DEV_STICKS; i++) {
		const dev_stick *s = &p->input.stick[i];
		const float drift = (float)(s->dead > AX_FLAT ? s->dead : AX_FLAT) * 0.9f / AX_MAX;

		if (!s->present) continue;
		rig_init(&r, p);
		stick(&r, i, drift, -drift);
		stick(&r, i, -drift, drift);
		ck(count_true(r.st.held, IN_COUNT) == 0, "%s: the %s stick resting off its middle is still",
		   p->id, i ? "right" : "left");
		for (d = 0; d < 4; d++) {
			const in_button want = (in_button)(s->first + d);

			rig_init(&r, p);
			stick(&r, i, DIR[d][0], DIR[d][1]);
			ck(r.st.held[want] && count_true(r.st.held, IN_COUNT) == 1 && r.st.pressed[want],
			   "%s: the %s stick pushed is %s", p->id, i ? "right" : "left", in_info_of(want)->name);
			stick(&r, i, DIR[d][0] * 0.9f, DIR[d][1] * 0.9f);
			ck(r.presses[want] == 1, "%s: and held, one press", p->id);
			stick(&r, i, 0, 0);
			ck(!r.st.held[want] && !r.st.down[want], "%s: and let go, %s is up", p->id, in_info_of(want)->name);
			for (b = IN_LEFT; i == DEV_STICK_R && b <= IN_DOWN; b++)
				ck(!r.presses[b], "%s: the right stick is never the d-pad", p->id);
		}
	}
	/* The d-pad's key and the left stick together: held while either is */
	if (p->input.stick[DEV_STICK_L].present) {
		rig_init(&r, p);
		key(&r, code_of(p, IN_LEFT), 1);
		stick(&r, DEV_STICK_L, -1, 0);
		key(&r, code_of(p, IN_LEFT), 0);
		ck(r.st.down[IN_LEFT] && r.presses[IN_LEFT] == 1, "%s: LEFT held by the stick after its key", p->id);
		stick(&r, DEV_STICK_L, 0, 0);
		ck(!r.st.down[IN_LEFT], "%s: and up when both let go", p->id);
	}
}

/* ---- the tutorial --------------------------------------------------------------- */

typedef enum { X_DONE, X_WRONG, X_QUIET, X_SKIP, X_MOVE, X_ACCEPT, X_CALLER } outcome;

static const char *const OUTCOME[] = { "its press", "a wrong press", "nothing", "the skip", "a move",
                                       "MENU, waiting for X", "the launcher's" };

/* What the tutorial does with button `b` alone in lesson `s` on `p`, as
 * DESIGN.md 11.10's table has it, written here and not read from tour.c */
static outcome expect(const device_profile *p, tour_step s, in_button b)
{
	const in_kind kind = in_info_of(b)->kind;

	if (s == TOUR_UNLOCK && (b == IN_SELECT || b == IN_START)) return X_QUIET;
	if (b == IN_START) return X_SKIP;
	if (b == IN_POWER) return s == TOUR_POWER ? X_DONE : X_CALLER;
	if (b == IN_VOLUP || b == IN_VOLDN)
		return s == TOUR_VOL ? X_DONE : s == TOUR_BRIGHT ? X_WRONG : X_QUIET;
	if (b == IN_BRIGHTUP || b == IN_BRIGHTDN) return s == TOUR_BRIGHT ? X_DONE : X_QUIET;
	if (s == TOUR_MOVE && (kind == IK_DPAD || b == IN_L1 || b == IN_R1)) return X_MOVE;
	if (s == TOUR_QUIT && b == IN_MENU) return X_ACCEPT;
	if ((s == TOUR_A && b == IN_ACCEPT) || (s == TOUR_B && b == IN_BACK) || (s == TOUR_X && b == IN_X) ||
	    (s == TOUR_Y && b == IN_Y) || (s == TOUR_MENU && b == IN_MENU) ||
	    (s == TOUR_SELECT && b == IN_SELECT))
		return X_DONE;
	(void)p;
	return X_WRONG;
}

/* The name Zyper's answer says for `b`, in English */
static const char *said(const device_profile *p, in_button b)
{
	switch (in_info_of(b)->kind) {
	case IK_DPAD:
		return b == IN_UP ? "Up" : b == IN_DOWN ? "Down" : b == IN_LEFT ? "Left" : "Right";
	case IK_STICK:  return "the right stick";
	case IK_LEVEL:  return b == IN_VOLUP || b == IN_VOLDN ? "volume" : "brightness";
	case IK_KEY:    if (b == IN_MENU) return dev_menu_word(p); break;
	case IK_FACE:
	case IK_SHOULDER:
	case IK_POWER:
	case IK_CLICK:  break;
	}
	return in_info_of(b)->name;
}

static outcome seen(const rig *r, int moves0)
{
	if (r->t.over && r->t.skipped) return X_SKIP;
	if (r->t.done) return X_DONE;
	if (r->t.wrong) return X_WRONG;
	if (r->t.step == TOUR_QUIT && r->t.moves == 1 && moves0 == 0) return X_ACCEPT;
	if (r->t.step == TOUR_MOVE && r->t.moves > moves0) return X_MOVE;
	return X_QUIET;
}

static void answered(rig *r, tour_step s, const char *what, outcome want, const char *name, const char *also)
{
	const outcome got = seen(r, 0);

	ck(got == want, "%s: %s in %s is %s (got %s)", r->p->id, what, tour_step_info((tour_step)s)->title, OUTCOME[want],
	   OUTCOME[got]);
	if (want == X_WRONG && got == X_WRONG) {
		ck(strstr(r->t.hint, name) != NULL, "%s: %s in %s is answered by its name %s: \"%s\"", r->p->id, what,
		   tour_step_info((tour_step)s)->title, name, r->t.hint);
		if (also)
			ck(strstr(r->t.hint, also) != NULL, "%s: %s in %s says %s too: \"%s\"", r->p->id, what,
			   tour_step_info((tour_step)s)->title, also, r->t.hint);
		/* Never MENU for the R36S's FN */
		if (strcmp(dev_menu_word(r->p), "MENU"))
			ck(strstr(r->t.hint, "MENU") == NULL, "%s: %s in %s says %s, not MENU: \"%s\"", r->p->id, what,
			   tour_step_info((tour_step)s)->title, dev_menu_word(r->p), r->t.hint);
	}
	ck(!r->glyph_fail[0], "%s: %s in %s: %s", r->p->id, what, tour_step_info((tour_step)s)->title, r->glyph_fail);
}

static void tutorial(const device_profile *p)
{
	static const float DIR[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
	const char *menu = dev_menu_word(p);
	const bool fn = p->chords.bright == DEV_BRIGHT_MENU_VOL;
	char what[64], also[32];
	int s, i, d;
	rig r;

	tour_menu_word(menu);
	tour_menu_keys(IN_L1, IN_L2, "MENU+L1", "MENU+L2");
	tour_lock_hold(2000);
	for (s = TOUR_FIRST_LESSON; s < TOUR_FIRST_LESSON + TOUR_LESSONS; s++) {
		/* Every code alone */
		for (i = 0; i < p->input.n_keys; i++) {
			const in_button b = p->input.keys[i].b;
			outcome want = expect(p, (tour_step)s, b);
			const char *ext = NULL;

			rig_init(&r, p);
			tour_on(&r, (tour_step)s);
			key(&r, p->input.keys[i].code, 1);
			key(&r, p->input.keys[i].code, 0);
			idle(&r);
			if (want == X_CALLER) continue;   /* POWER: the handheld turns off */
			snprintf(what, sizeof what, "%s", in_info_of(b)->name);
			/* The answers that name the chord: the lock's key alone, MENU
			 * alone in the lock's and the brightness's, a volume key alone
			 * where brightness is MENU with it */
			if (s == TOUR_LOCK && (b == IN_L2 || b == IN_MENU)) ext = menu;
			if (s == TOUR_LOCK && b == IN_MENU) ext = "L2";
			if (s == TOUR_QUIT && b == IN_X) ext = menu;
			if (s == TOUR_BRIGHT && fn && (b == IN_VOLUP || b == IN_VOLDN || b == IN_MENU)) {
				answered(&r, (tour_step)s, what, want, menu, NULL);
				continue;
			}
			answered(&r, (tour_step)s, what, want, said(p, b), ext);
		}
		/* Every stick, every way */
		for (i = 0; i < DEV_STICKS; i++) {
			const dev_stick *st = &p->input.stick[i];

			if (!st->present) continue;
			for (d = 0; d < 4; d++) {
				const in_button b = (in_button)(st->first + d);

				rig_init(&r, p);
				tour_on(&r, (tour_step)s);
				stick(&r, i, DIR[d][0], DIR[d][1]);
				stick(&r, i, 0, 0);
				idle(&r);
				snprintf(what, sizeof what, "the %s stick %s", i ? "right" : "left",
				         i ? in_info_of(b)->name + 3 : in_info_of(b)->name);
				answered(&r, (tour_step)s, what, expect(p, (tour_step)s, b), said(p, b), NULL);
			}
		}
		/* The chords: the lock's and the screenshot's, and brightness */
		rig_init(&r, p);
		tour_on(&r, (tour_step)s);
		chord(&r, IN_L2);
		snprintf(what, sizeof what, "%s+L2", menu);
		answered(&r, (tour_step)s, what, s == TOUR_LOCK ? X_DONE : X_QUIET, "", NULL);
		ck(g_locks == 1, "%s: %s in %s locks the stage, not the handheld", p->id, what, tour_step_info((tour_step)s)->title);
		rig_init(&r, p);
		tour_on(&r, (tour_step)s);
		chord(&r, IN_L1);
		snprintf(what, sizeof what, "%s+L1", menu);
		answered(&r, (tour_step)s, what, X_QUIET, "", NULL);
		ck(g_shots == 1, "%s: %s in %s takes the screenshot", p->id, what, tour_step_info((tour_step)s)->title);
		if (fn) {
			rig_init(&r, p);
			tour_on(&r, (tour_step)s);
			chord(&r, IN_VOLUP);
			snprintf(what, sizeof what, "%s+VOL+", menu);
			answered(&r, (tour_step)s, what, s == TOUR_BRIGHT ? X_DONE : X_QUIET, "", NULL);
		}
	}

	/* The lessons of two presses and a hold, whole */
	rig_init(&r, p);
	tour_on(&r, TOUR_QUIT);
	key(&r, code_of(p, IN_MENU), 1);
	key(&r, code_of(p, IN_MENU), 0);
	idle(&r);
	key(&r, code_of(p, IN_X), 1);
	key(&r, code_of(p, IN_X), 0);
	snprintf(what, sizeof what, "%s, X", menu);
	answered(&r, TOUR_QUIT, what, X_DONE, "", NULL);

	rig_init(&r, p);
	tour_on(&r, TOUR_UNLOCK);
	key(&r, code_of(p, IN_SELECT), 1);
	key(&r, code_of(p, IN_START), 1);
	for (i = 0; i < 2100 / 40; i++) idle(&r);
	answered(&r, TOUR_UNLOCK, "SELECT+START held", X_DONE, "", NULL);

	rig_init(&r, p);
	tour_on(&r, TOUR_MOVE);
	key(&r, code_of(p, IN_RIGHT), 1);
	key(&r, code_of(p, IN_RIGHT), 0);
	key(&r, code_of(p, IN_LEFT), 1);
	key(&r, code_of(p, IN_LEFT), 0);
	answered(&r, TOUR_MOVE, "two moves", X_DONE, "", NULL);
	if (p->input.stick[DEV_STICK_L].present) {
		rig_init(&r, p);
		tour_on(&r, TOUR_MOVE);
		stick(&r, DEV_STICK_L, 1, 0);
		stick(&r, DEV_STICK_L, 0, 0);
		stick(&r, DEV_STICK_L, -1, 0);
		stick(&r, DEV_STICK_L, 0, 0);
		answered(&r, TOUR_MOVE, "the left stick twice", X_DONE, "", NULL);
	}
	(void)also;
}

int main(void)
{
	const device_profile *p;
	int i;

	i18n_set(LANG_EN);
	model();
	for (i = 0; (p = plat_profile_at(i)); i++) {
		int before = fails;

		printf("%s:\n", p->id);
		table(p);
		codes(p);
		chords(p);
		capture(p);
		sticks(p);
		tutorial(p);
		printf("  %s\n", fails == before ? "ok" : "FAILED");
	}
	ck(i >= 5, "every profile walked (%d)", i);
	if (fails) {
		printf("\n%d of %d input model check(s) FAILED\n", fails, checks);
		return 1;
	}
	printf("ok: the input model, %d checks on %d profiles\n", checks, i);
	return 0;
}
