/* Volume and brightness while a game runs. See levels.h.
 *
 * Until 2026-10-02 all of this was in the ports, each with its own copy: the
 * positions, the step, the rescale, and - in both of the device ports - the
 * same hold-to-repeat. A port deals in pixels, samples, buttons and time, and
 * nothing here is any of those. */
#include "levels.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "diatom.h"
#include "../port/boost.h"

/* Where each level is, or -1 when it is not known and the hardware has to be
 * asked. */
static int g_pos[DIATOM_LEVEL_COUNT] = { -1, -1 };

/* The boost past the top of each (port/boost.h, as the launcher's
 * src/levels.c keeps it): its bar on the port's scale once the port's
 * positions are known, and what it last gave the sound or the display, the
 * gain in dB for the volume and the lift for the brightness. Both 0 at the
 * start, which is what the sound and the table start at. A game is a place
 * where both open (BOOST_OPEN). */
static const int BOOST_CELLS[DIATOM_LEVEL_COUNT] = { BOOST_VOL_CELLS, BOOST_BRIGHT_CELLS };
static const int BAR_KIND[DIATOM_LEVEL_COUNT]    = { OSDBAR_VOLUME, OSDBAR_BRIGHT };
static const uint32_t UP_KEY[DIATOM_LEVEL_COUNT] = {
	DIATOM_LEVEL_KEY_VOLUME_UP, DIATOM_LEVEL_KEY_BRIGHTNESS_UP
};
static struct {
	boost_bar bar;
	bool      ready;
	int       told;
	bool      up_was;      /* its up key was held last frame */
} g_boost[DIATOM_LEVEL_COUNT];

/* The bar as this side brought it up: which kind, and when it last changed,
 * the clock the port's own envelope (osdbar_envelope) runs on from the same
 * call, so this knows when it goes away without asking the port. And the
 * look last handed over, so it is handed again only when it moves. */
static bool              g_bar_up;
static diatom_level_kind g_bar_kind;
static uint64_t          g_bar_changed_us;
static osdbar_look       g_look_told;
static bool              g_look_said;

/* The rumble as the boost opens, the launcher's, and as a screenshot is
 * taken: this long, at the game's rumble level, ended on the first frame
 * after (0, none running) */
#define BUZZ_MS 40
static uint64_t g_buzz_until_us;

void diatom_levels_buzz(void)
{
	diatom_rumble_buzz(true);
	g_buzz_until_us = diatom_port_now_us() + BUZZ_MS * 1000ull;
}

static unsigned g_presses;      /* diatom_levels_presses */

static uint32_t ms_of(uint64_t us) { return (uint32_t)(us / 1000u); }

void diatom_levels_invalidate(void)
{
	int k;

	for (k = 0; k < DIATOM_LEVEL_COUNT; k++) g_pos[k] = -1;
	diatom_port_level_invalidate();
}

/* The state plane's rescale: round-to-nearest, endpoints exact. The endpoints
 * matter most - they are where a user is most likely to sit, and a minimum
 * that drifts off silence after a few round trips is the bug this whole design
 * exists to prevent. */
static int rescale(int index, int from, int to)
{
	if (from <= 1 || to <= 1) return 0;
	if (index < 0)        index = 0;
	if (index > from - 1) index = from - 1;
	return (index * (to - 1) + (from - 1) / 2) / (from - 1);
}

/* The position, read from the hardware the first time after a handover, so the
 * first press moves one step from where the launcher left it. */
static int position(diatom_level_kind kind)
{
	if (g_pos[kind] < 0) g_pos[kind] = diatom_port_level_read(kind);
	return g_pos[kind];
}

/* The boost's effect: the volume's gain to the sound, the brightness's lift
 * to the port's gamma table, each said only when it changes */
static void boost_apply(diatom_level_kind kind)
{
	const boost_bar *b = &g_boost[kind].bar;
	const int v = kind == DIATOM_LEVEL_VOLUME ? boost_db(b) : boost_lift(b);

	if (v == g_boost[kind].told) return;
	g_boost[kind].told = v;
	if (kind == DIATOM_LEVEL_VOLUME) diatom_audio_boost(v);
	else diatom_port_level_lift(v);
}

/* The boost's bar on the port's scale, at the level as it is now: another
 * path may have moved it (SETLEVEL, a read after a handover), and one under
 * the top closes the boost. False while the port's positions or the level are
 * not known, or there is only the one position. */
static bool boost_sync(diatom_level_kind kind)
{
	const int positions = diatom_port_level_positions(kind);
	boost_bar *b = &g_boost[kind].bar;

	if (positions <= 1 || position(kind) < 0) return false;
	if (!g_boost[kind].ready || b->max != positions - 1) {
		boost_init(b, g_pos[kind], positions - 1, BOOST_CELLS[kind], 0);
		g_boost[kind].ready = true;
	} else if (b->level != g_pos[kind]) {
		boost_set_level(b, g_pos[kind]);
	}
	boost_apply(kind);
	return true;
}

bool diatom_levels_get(diatom_level_kind kind, int *index, int *count)
{
	int positions = diatom_port_level_positions(kind);

	if (positions <= 0 || position(kind) < 0) return false;
	*index = g_pos[kind];
	*count = positions;
	return true;
}

bool diatom_levels_set(diatom_level_kind kind, int index, int count, int boost)
{
	int positions = diatom_port_level_positions(kind);
	boost_bar *b = &g_boost[kind].bar;

	if (positions <= 0) return false;
	g_pos[kind] = rescale(index, count, positions);
	diatom_port_level_write(kind, g_pos[kind]);
	/* The boost the launcher kept with the level: open at the top, at as
	 * many cells as it says, and none under it. A launcher from before the
	 * boost says none, and none is what it has. */
	if (boost_sync(kind)) {
		if (b->level == b->max && boost > 0) {
			if (!b->open || b->cells != boost) boost_open_at(b, boost);
		} else if (b->open) {
			boost_shut(b);
		}
		boost_apply(kind);
	}
	return true;
}

int diatom_levels_boost(diatom_level_kind kind)
{
	const boost_bar *b;

	if ((unsigned)kind >= DIATOM_LEVEL_COUNT || !g_boost[kind].ready) return 0;
	b = &g_boost[kind].bar;
	return b->open ? b->cells : 0;
}

unsigned diatom_levels_presses(void) { return g_presses; }

/* The bar's look for `kind` now, handed to the port when it differs from the
 * one handed last. Worked out on every call, not only on a change: the mark
 * follows the level through boost_look, which starts its move there. */
static void look_hand(diatom_level_kind kind, uint64_t now)
{
	osdbar_look l;

	if (!g_boost[kind].ready) return;
	boost_look(&g_boost[kind].bar, BAR_KIND[kind], BOOST_OPEN, ms_of(now), &l);
	if (g_look_said && !memcmp(&l, &g_look_told, sizeof l)) return;
	g_look_told = l;
	g_look_said = true;
	diatom_port_level_look(&l);
}

/* The bar for `kind` at its level, brought up or held. The look goes first,
 * so the port never draws this kind's bar from a look of before. */
static void show(diatom_level_kind kind)
{
	const uint64_t now = diatom_port_now_us();

	g_bar_up = true;
	g_bar_kind = kind;
	g_bar_changed_us = now;
	look_hand(kind, now);
	diatom_port_level_shown(kind, g_pos[kind], diatom_port_level_positions(kind));
}

/* One step, written and shown even at an end of the scale, so pressing past
 * the top still shows the bar full. */
static void step(diatom_level_kind kind, int dir)
{
	int positions = diatom_port_level_positions(kind), pos;

	if (positions <= 0 || position(kind) < 0) return;
	pos = g_pos[kind] + dir;
	if (pos < 0)              pos = 0;
	if (pos > positions - 1)  pos = positions - 1;
	g_pos[kind] = pos;
	diatom_port_level_write(kind, pos);
	show(kind);
}

/* What a press or the boost's clock did (BOOST_LEVEL...), done: the level by
 * the step as before, a press at an end that moved nothing too (written and
 * shown, as it always was), the bar alone for the cells, the boost's effect,
 * the rumble as it opens. A held key's repeat the boost stopped (the key that
 * opened it, still down) moves nothing and only holds the bar up. */
static void act(diatom_level_kind kind, int dir, int did)
{
	const boost_bar *b = &g_boost[kind].bar;

	if (did & BOOST_GAIN) boost_apply(kind);
	if (did & BOOST_LEVEL)
		step(kind, dir);
	else if (!did && (dir > 0 ? b->level == b->max : b->level == 0))
		step(kind, dir);
	else
		show(kind);
	if (did & BOOST_OPENED) diatom_levels_buzz();
}

/* A level key's press (`repeat` false) or a held key's repeat */
static void key(diatom_level_kind kind, int dir, bool repeat, uint64_t now)
{
	g_presses++;
	if (!boost_sync(kind)) {
		step(kind, dir);
		return;
	}
	act(kind, dir, boost_key(&g_boost[kind].bar, dir, repeat, BOOST_OPEN, ms_of(now)));
}

/* Holding a level key repeats it at the launcher's pace - ZyperOS's
 * REPEAT_DELAY_MS and REPEAT_RATE_MS, 300 and 90 - so a hold does the same thing
 * in a game as at the shelf. */
#define REPEAT_DELAY_US 300000ull
#define REPEAT_RATE_US   90000ull

static const struct {
	uint32_t          key;
	diatom_level_kind kind;
	int               dir;
} level_keys[] = {
	{ DIATOM_LEVEL_KEY_VOLUME_UP,       DIATOM_LEVEL_VOLUME,     +1 },
	{ DIATOM_LEVEL_KEY_VOLUME_DOWN,     DIATOM_LEVEL_VOLUME,     -1 },
	{ DIATOM_LEVEL_KEY_BRIGHTNESS_UP,   DIATOM_LEVEL_BRIGHTNESS, +1 },
	{ DIATOM_LEVEL_KEY_BRIGHTNESS_DOWN, DIATOM_LEVEL_BRIGHTNESS, -1 },
};
#define LEVEL_KEYS (sizeof level_keys / sizeof level_keys[0])

static uint64_t g_next_us[LEVEL_KEYS];   /* when a held key next repeats; 0 is not held */

void diatom_levels_frame(void)
{
	uint32_t held = diatom_port_level_keys();
	const uint64_t now = diatom_port_now_us();
	size_t i;
	int k;

	for (i = 0; i < LEVEL_KEYS; i++) {
		bool repeat;

		if (!(held & level_keys[i].key)) {
			g_next_us[i] = 0;
			continue;
		}
		if (!g_next_us[i]) {
			g_next_us[i] = now + REPEAT_DELAY_US;
			repeat = false;
		} else if (now >= g_next_us[i]) {
			g_next_us[i] = now + REPEAT_RATE_US;
			repeat = true;
		} else {
			continue;
		}
		key(level_keys[i].kind, level_keys[i].dir, repeat, now);
	}

	/* The boost's clock: the up key let go, a hold that charged long enough
	 * opening it, presses that stopped draining */
	for (k = 0; k < DIATOM_LEVEL_COUNT; k++) {
		const bool up = (held & UP_KEY[k]) != 0;
		boost_bar *b = &g_boost[k].bar;
		int did;

		if (!g_boost[k].ready || g_pos[k] < 0) continue;
		if (g_boost[k].up_was && !up) boost_release(b, ms_of(now));
		g_boost[k].up_was = up;
		did = boost_tick(b, up, BOOST_OPEN, ms_of(now));
		if (did) act((diatom_level_kind)k, +1, did & ~BOOST_LEVEL);
	}

	/* A plug in or out closes the volume boost, so the new output starts at
	 * the top without it (the launcher's pick, src/levels.c) */
	if (diatom_port_level_plugged() && g_boost[DIATOM_LEVEL_VOLUME].bar.open) {
		boost_shut(&g_boost[DIATOM_LEVEL_VOLUME].bar);
		boost_apply(DIATOM_LEVEL_VOLUME);
		printf("diatom: the output changed, so the volume boost is closed\n");
	}

	/* The bar: its look while it is up; once it has gone, an empty boost
	 * closes and a filled one stays */
	if (g_bar_up) {
		if (now - g_bar_changed_us >= (uint64_t)(OSDBAR_HOLD_MS + OSDBAR_OUT_MS) * 1000u) {
			g_bar_up = false;
			for (k = 0; k < DIATOM_LEVEL_COUNT; k++) {
				if (!g_boost[k].ready) continue;
				boost_bar_gone(&g_boost[k].bar);
				boost_apply((diatom_level_kind)k);
			}
		} else {
			look_hand(g_bar_kind, now);
		}
	}

	if (g_buzz_until_us && now >= g_buzz_until_us) {
		g_buzz_until_us = 0;
		diatom_rumble_buzz(false);
	}
}
