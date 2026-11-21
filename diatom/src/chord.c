/* SELECT as the hotkeys' chord key: the rule is in chord.h. */
#include "chord.h"

#include <string.h>

/* Where a press of SELECT is:
 *
 *   CS_IDLE   SELECT is up
 *   CS_WAIT   down, hidden, waiting to see whose it is
 *   CS_CHORD  the hotkeys': hidden until let go, the shoulders are hotkeys
 *   CS_HELD   held alone past CHORD_HOLD_MS: seen, held, by the game, and
 *             the shoulders are still hotkeys until another button goes down
 *   CS_GAME   the game's: seen, the shoulders are the game's */
enum { CS_IDLE, CS_WAIT, CS_CHORD, CS_HELD, CS_GAME };

#define SEL  DIATOM_BIT(DIATOM_BTN_SELECT)
#define MENU DIATOM_BIT(DIATOM_BTN_MENU)

/* A press of SELECT let go is given back at least this many turns: one turn
 * is a frame, and fake08 reads the pad on every other frame of a 30 fps
 * cart, so a one-turn press could land between two of its reads. */
#define OWE_MIN 2

void chord_default_keys(int key[CHORD_ACTS])
{
	int i;

	for (i = 0; i < CHORD_ACTS; i++) key[i] = -1;
	key[CHORD_SAVE]   = DIATOM_BTN_R1;
	key[CHORD_LOAD]   = DIATOM_BTN_L1;
	key[CHORD_FASTER] = DIATOM_BTN_R2;
	key[CHORD_SLOWER] = DIATOM_BTN_L2;
}

void chord_reset(chord *c)
{
	memset(c, 0, sizeof *c);
	chord_default_keys(c->key);
	c->keyed = true;
}

void chord_set_keys(chord *c, const int key[CHORD_ACTS])
{
	memcpy(c->key, key, sizeof c->key);
	c->keyed = true;
	/* A shoulder held as the keys change is no lead of the new ones */
	memset(c->key_ms, 0, sizeof c->key_ms);
}

static const char *const ACT_NAME[CHORD_ACTS] = {
	"save", "load", "faster", "slower", "shot", "shader", "display", "reset", "quit"
};

/* The buttons a hotkey can be held with: never SELECT, its holder, nor
 * MENU, Diatom's own; never the d-pad, the stick's and the game's */
static const struct { const char *name; int id; } KEY_NAME[] = {
	{ "a", DIATOM_BTN_A }, { "b", DIATOM_BTN_B }, { "x", DIATOM_BTN_X }, { "y", DIATOM_BTN_Y },
	{ "l1", DIATOM_BTN_L1 }, { "r1", DIATOM_BTN_R1 }, { "l2", DIATOM_BTN_L2 },
	{ "r2", DIATOM_BTN_R2 }, { "start", DIATOM_BTN_START },
};

static int name_at(const char *s, size_t n, bool act)
{
	size_t i;

	if (act) {
		for (i = 0; i < CHORD_ACTS; i++)
			if (strlen(ACT_NAME[i]) == n && !strncmp(ACT_NAME[i], s, n)) return (int)i;
	} else {
		for (i = 0; i < sizeof KEY_NAME / sizeof KEY_NAME[0]; i++)
			if (strlen(KEY_NAME[i].name) == n && !strncmp(KEY_NAME[i].name, s, n)) return KEY_NAME[i].id;
	}
	return -1;
}

bool chord_keys_parse(const char *spec, int key[CHORD_ACTS])
{
	int next[CHORD_ACTS], i;
	const char *p = spec;

	if (!spec) return false;
	for (i = 0; i < CHORD_ACTS; i++) next[i] = -1;
	if (strcmp(spec, "none")) {
		while (*p) {
			const char *end = strchr(p, ','), *colon;
			size_t n = end ? (size_t)(end - p) : strlen(p);
			int a, k;

			colon = memchr(p, ':', n);
			if (!colon) return false;
			a = name_at(p, (size_t)(colon - p), true);
			k = name_at(colon + 1, n - (size_t)(colon + 1 - p), false);
			if (a < 0 || k < 0 || next[a] >= 0) return false;
			for (i = 0; i < CHORD_ACTS; i++)
				if (next[i] == k) return false;
			next[a] = k;
			if (!end) break;
			p = end + 1;
		}
	}
	memcpy(key, next, sizeof next);
	return true;
}

uint32_t chord_key_of(const chord *c, chord_act a)
{
	return (unsigned)a < CHORD_ACTS && c->key[a] >= 0 ? DIATOM_BIT(c->key[a]) : 0;
}

uint32_t chord_keys(const chord *c)
{
	uint32_t k = 0;
	int i;

	for (i = 0; i < CHORD_ACTS; i++) k |= chord_key_of(c, (chord_act)i);
	return k;
}

/* The shoulders still held that went down at most CHORD_LEAD_MS ago, and
 * not this turn: SELECT's partners, pressed a moment before it */
static uint32_t leads(const chord *c, uint32_t held, uint32_t pressed,
                      uint64_t now_ms)
{
	uint32_t l = 0;
	int i;

	for (i = 0; i < CHORD_ACTS; i++) {
		const uint32_t k = chord_key_of(c, (chord_act)i);

		if (k && (held & k) && !(pressed & k) && now_ms - c->key_ms[i] <= CHORD_LEAD_MS)
			l |= k;
	}
	return l;
}

static unsigned acts_of(const chord *c, uint32_t keys)
{
	unsigned a = 0;
	int i;

	for (i = 0; i < CHORD_ACTS; i++)
		if (keys & chord_key_of(c, (chord_act)i)) a |= 1u << i;
	return a;
}

/* A press of `n` turns owed to the game. One let go while another is still
 * being given waits for it and a turn with SELECT up between them, so the
 * game reads two presses and not one long one. */
static void owe(chord *c, int n)
{
	if (n < OWE_MIN) n = OWE_MIN;
	if (c->owed > 0 || c->gap) c->next = n;
	else c->owed = n;
}

uint32_t chord_view(chord *c, uint32_t held, uint32_t was, uint32_t pressed,
                    uint32_t live)
{
	uint32_t tap;

	if (!c->keyed) chord_reset(c);
	tap = pressed & ~held & live & chord_keys(c);
	return (was & SEL) && tap ? held | tap | SEL : held;
}

chord_out chord_turn(chord *c, uint32_t held, uint32_t was, uint64_t now_ms,
                     uint32_t live)
{
	chord_out o = { 0 };
	const uint32_t pressed = held & ~was;
	uint32_t hot;
	/* What else went down this turn: MENU is Diatom's, not a game button */
	const uint32_t others = pressed & ~SEL & ~MENU;
	uint32_t lead = 0;
	int i;

	/* A zeroed chord never reset has the shoulders' keys */
	if (!c->keyed) chord_reset(c);
	hot = live & chord_keys(c);
	/* A shoulder a chord took is the hotkeys' until it is let go, as MENU's
	 * chords hold theirs: the frame it goes up is the first the game may
	 * have it again. */
	c->hidden &= held;
	for (i = 0; i < CHORD_ACTS; i++)
		if (pressed & chord_key_of(c, (chord_act)i)) c->key_ms[i] = now_ms;

	switch (c->state) {
	case CS_IDLE:
		if (!(pressed & SEL)) break;
		/* A shoulder a moment before it is pressed with it, as one that
		 * comes in the same turn: the game has had it for those turns,
		 * and has it no more from this one. Not one a chord has taken. */
		lead = leads(c, held, pressed, now_ms) & hot & ~c->hidden;
		/* Pressed together, in one turn: what came with it decides */
		if ((others & hot) || lead) c->state = CS_CHORD;
		else if (others) c->state = CS_GAME;
		else {
			c->state = CS_WAIT;
			c->down_ms = now_ms;
			c->held = 0;
		}
		break;
	case CS_WAIT:
		if (!(held & SEL)) {
			owe(c, c->held);
			c->state = CS_IDLE;
		} else if (others & hot) {
			c->state = CS_CHORD;
		} else if (others) {
			/* With the other button, in this same turn: from this turn
			 * on */
			c->state = CS_GAME;
		} else if (now_ms - c->down_ms >= CHORD_HOLD_MS) {
			/* Held alone: the game's, held, from this turn on; a
			 * shoulder is still a hotkey */
			c->state = CS_HELD;
		}
		break;
	case CS_HELD:
		if (!(held & SEL)) c->state = CS_IDLE;
		else if (others & ~hot) c->state = CS_GAME;
		break;
	default:
		if (!(held & SEL)) c->state = CS_IDLE;
		break;
	}

	if (c->state == CS_CHORD || c->state == CS_HELD) {
		uint32_t t = (pressed & hot) | lead;

		o.took = t;
		o.acts = acts_of(c, t);
		c->hidden |= t;
	}
	if (c->state == CS_WAIT) c->held++;
	if (c->state == CS_WAIT || c->state == CS_CHORD) o.hide |= SEL;
	o.hide |= c->hidden;

	if (c->owed > 0) {
		o.give |= SEL;
		if (--c->owed == 0 && c->next) c->gap = true;
	} else if (c->gap) {
		c->gap = false;
		c->owed = c->next;
		c->next = 0;
	}
	return o;
}
