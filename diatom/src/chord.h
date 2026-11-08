/* SELECT as the hotkeys' chord key, without taking SELECT from the games
 * that play with it.
 *
 * The hotkeys are SELECT held and a shoulder pressed: SELECT+R1 saves a
 * state to the quick slot, SELECT+L1 loads it, SELECT+R2 runs the game
 * faster and SELECT+L2 slower. Most systems have a SELECT of their own (NES,
 * SNES, Game Boy, GBA, PlayStation...), so a press of SELECT belongs either to
 * the game or to the hotkeys, decided by what comes next, and never to both:
 *
 *   SELECT, then R1, L1, R2 or L2 while it is held (and before any other
 *       button): a hotkey. The game never sees that SELECT, nor the shoulder.
 *       More shoulders under the same SELECT are hotkeys too.
 *   SELECT, then any other button while it is held: the game's, at once,
 *       held with that button. A+B+START+SELECT, the soft reset, still works.
 *   SELECT alone, let go: the game's, as a press as long as the one made,
 *       given to the game as it is let go.
 *   SELECT alone, held for CHORD_HOLD_MS: the game's from then on, held.
 *
 * Once a SELECT is the game's, the shoulders are the game's too until SELECT
 * is let go. A system with no SELECT (the N64, the PICO-8) loses nothing: a
 * SELECT given late reaches a core that ignores it.
 *
 * Pure: no clock of its own and no port, so test/chord_test.c drives it turn
 * by turn. A turn is one turn of the frame loop, one input poll. */
#ifndef DIATOM_CHORD_H
#define DIATOM_CHORD_H

#include <stdbool.h>
#include <stdint.h>

#include "diatom_port.h"

/* How long SELECT alone waits for a shoulder before it is the game's. A
 * deliberate chord comes well inside it; a game that wants SELECT held gets
 * it this late. */
#define CHORD_HOLD_MS 400

/* What a chord asks for */
typedef enum {
	CHORD_SAVE,       /* SELECT+R1 */
	CHORD_LOAD,       /* SELECT+L1 */
	CHORD_FASTER,     /* SELECT+R2 */
	CHORD_SLOWER,     /* SELECT+L2 */
	CHORD_ACTS
} chord_act;

/* The shoulder each act is on */
#define CHORD_KEYS (DIATOM_BIT(DIATOM_BTN_R1) | DIATOM_BIT(DIATOM_BTN_L1) | \
                    DIATOM_BIT(DIATOM_BTN_R2) | DIATOM_BIT(DIATOM_BTN_L2))

typedef struct {
	int      state;       /* chord.c's CS_* */
	uint64_t down_ms;     /* when SELECT went down */
	int      held;        /* turns SELECT has been held while it waits */
	int      owed;        /* turns of SELECT still to give the game */
	int      next;        /* a second press's turns, owed after the first's */
	bool     gap;         /* the turn between the two, SELECT up */
	uint32_t hidden;      /* shoulders a chord took, hidden until let go */
} chord;

/* One turn's answer */
typedef struct {
	uint32_t hide;        /* buttons the core must not see this turn */
	uint32_t give;        /* buttons the core sees held, though they are not */
	uint32_t took;        /* shoulders that went down this turn as a chord */
	unsigned acts;        /* the acts this turn, 1 << chord_act */
} chord_out;

void chord_reset(chord *c);

/* One turn. `held` is the pad now, `was` the pad at the last turn, `now_ms`
 * a clock in milliseconds, and `live` which of CHORD_KEYS are hotkeys now (a
 * game whose core keeps no state has no save or load, and R1 and L1 are its
 * own under SELECT). MENU is Diatom's and counts as no button. */
chord_out chord_turn(chord *c, uint32_t held, uint32_t was, uint64_t now_ms,
                     uint32_t live);

#endif
