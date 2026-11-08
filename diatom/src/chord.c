/* SELECT as the hotkeys' chord key: the rule is in chord.h. */
#include "chord.h"

#include <string.h>

/* Where a press of SELECT is:
 *
 *   CS_IDLE   SELECT is up
 *   CS_WAIT   down, hidden, waiting to see whose it is
 *   CS_CHORD  the hotkeys': hidden until let go, the shoulders are hotkeys
 *   CS_GAME   the game's: seen, the shoulders are the game's */
enum { CS_IDLE, CS_WAIT, CS_CHORD, CS_GAME };

#define SEL  DIATOM_BIT(DIATOM_BTN_SELECT)
#define MENU DIATOM_BIT(DIATOM_BTN_MENU)

/* A press of SELECT let go is given back at least this many turns: one turn
 * is a frame, and fake08 reads the pad on every other frame of a 30 fps
 * cart, so a one-turn press could land between two of its reads. */
#define OWE_MIN 2

static const int ACT_KEY[CHORD_ACTS] = {
	[CHORD_SAVE]   = DIATOM_BTN_R1,
	[CHORD_LOAD]   = DIATOM_BTN_L1,
	[CHORD_FASTER] = DIATOM_BTN_R2,
	[CHORD_SLOWER] = DIATOM_BTN_L2,
};

void chord_reset(chord *c)
{
	memset(c, 0, sizeof *c);
}

static unsigned acts_of(uint32_t keys)
{
	unsigned a = 0;
	int i;

	for (i = 0; i < CHORD_ACTS; i++)
		if (keys & DIATOM_BIT(ACT_KEY[i])) a |= 1u << i;
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

chord_out chord_turn(chord *c, uint32_t held, uint32_t was, uint64_t now_ms,
                     uint32_t live)
{
	chord_out o = { 0 };
	const uint32_t pressed = held & ~was;
	const uint32_t hot = live & CHORD_KEYS;
	/* What else went down this turn: MENU is Diatom's, not a game button */
	const uint32_t others = pressed & ~SEL & ~MENU;

	/* A shoulder a chord took is the hotkeys' until it is let go, as MENU's
	 * chords hold theirs: the frame it goes up is the first the game may
	 * have it again. */
	c->hidden &= held;

	switch (c->state) {
	case CS_IDLE:
		if (!(pressed & SEL)) break;
		/* Pressed together, in one turn: what came with it decides */
		if (others & hot) c->state = CS_CHORD;
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
		} else if (others || now_ms - c->down_ms >= CHORD_HOLD_MS) {
			/* With the other button, in this same turn, or held: from
			 * this turn on */
			c->state = CS_GAME;
		}
		break;
	default:
		if (!(held & SEL)) c->state = CS_IDLE;
		break;
	}

	if (c->state == CS_CHORD) {
		uint32_t t = pressed & hot;

		o.took = t;
		o.acts = acts_of(t);
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
