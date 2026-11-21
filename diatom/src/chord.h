/* SELECT as the hotkeys' chord key, without taking SELECT from the games
 * that play with it.
 *
 * The hotkeys are SELECT held and one more button pressed: SELECT+R1 saves
 * a state to the quick slot, SELECT+L1 loads it, SELECT+R2 runs the game
 * faster and SELECT+L2 slower, until the player gives them other buttons;
 * a screenshot, the next shader and display mode, Reset Game and Quit have
 * none until given one (the launcher's Hotkeys, RUN hotkeys= and
 * SETHOTKEYS). Below, "a shoulder" is any button an act is on. Most systems have a SELECT of their own (NES,
 * SNES, Game Boy, GBA, PlayStation...), so a press of SELECT belongs either to
 * the game or to the hotkeys, decided by what comes next, and never to both:
 *
 *   SELECT, then R1, L1, R2 or L2 while it is held (and before any other
 *       button): a hotkey. The game never sees that SELECT, nor the shoulder.
 *       More shoulders under the same SELECT are hotkeys too.
 *   A shoulder, then SELECT at most CHORD_LEAD_MS later, the shoulder still
 *       held: the same hotkey, on SELECT's turn. Two buttons pressed as one
 *       reach the pad a frame or two apart, either one first; the game has
 *       had the shoulder for those frames, and has it no more from then.
 *   SELECT, then any other button while it is held: the game's, at once,
 *       held with that button. A+B+START+SELECT, the soft reset, still works.
 *   SELECT alone, let go: the game's, as a press as long as the one made,
 *       given to the game as it is let go.
 *   SELECT alone, held for CHORD_HOLD_MS: the game's from then on, held;
 *       and a shoulder pressed after that, before any other button, is still
 *       a hotkey, so a player who holds SELECT and then finds R1 saves.
 *
 * Once another button has made a SELECT the game's, the shoulders are the
 * game's too until SELECT is let go. A system with no SELECT (the N64, the
 * PICO-8) loses nothing: a SELECT given late reaches a core that ignores it.
 *
 * Before 1.0.6 a shoulder a frame ahead of SELECT, or a SELECT held past
 * CHORD_HOLD_MS before the shoulder, was the game's: the hotkeys seemed to
 * work only now and then, and holding longer never helped.
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

/* How long before SELECT a shoulder still held may have gone down and be its
 * chord: two buttons pressed as one reach the pad a frame or two apart, in
 * either order. Long enough for that, short of any shoulder held for play. */
#define CHORD_LEAD_MS 100

/* What a chord asks for. The first four have keys from the start; the rest
 * are none until the player gives them one (the launcher's Hotkeys,
 * RUN hotkeys= and SETHOTKEYS: chord_keys_parse). */
typedef enum {
	CHORD_SAVE,       /* SELECT+R1 */
	CHORD_LOAD,       /* SELECT+L1 */
	CHORD_FASTER,     /* SELECT+R2 */
	CHORD_SLOWER,     /* SELECT+L2 */
	CHORD_SHOT,       /* a screenshot */
	CHORD_SHADER,     /* the system's next shader */
	CHORD_DISPLAY,    /* the system's next display mode */
	CHORD_RESET,      /* reset the game, asked first */
	CHORD_QUIT,       /* quit, as MENU then X */
	CHORD_ACTS
} chord_act;

/* The keys of the four that have one from the start, the shoulders */
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
	uint64_t key_ms[CHORD_ACTS]; /* when each act's shoulder last went down */
	/* Each act's button (DIATOM_BTN_*), or -1 for none: what SELECT is held
	 * with for it. The shoulders' four from chord_reset until chord_set_keys
	 * says otherwise. */
	int      key[CHORD_ACTS];
	bool     keyed;
} chord;

/* One turn's answer */
typedef struct {
	uint32_t hide;        /* buttons the core must not see this turn */
	uint32_t give;        /* buttons the core sees held, though they are not */
	uint32_t took;        /* shoulders that went down this turn as a chord */
	unsigned acts;        /* the acts this turn, 1 << chord_act */
} chord_out;

/* A fresh start for SELECT, with the keys from the start (the shoulders'
 * four); chord_set_keys after it for others */
void chord_reset(chord *c);

/* The keys for each act, CHORD_ACTS of them, -1 for none */
void chord_set_keys(chord *c, const int key[CHORD_ACTS]);

/* The keys from the start: R1, L1, R2, L2 for the four, none for the rest */
void chord_default_keys(int key[CHORD_ACTS]);

/* A launcher's keys, "save:r1,load:l1,shot:y" (an act's name and a button's,
 * a b x y l1 r1 l2 r2 start; "none" for none at all): the acts it names get
 * those buttons and the rest none. False, key untouched, for a bad pair, an
 * act or a button named twice, SELECT or MENU. */
bool chord_keys_parse(const char *spec, int key[CHORD_ACTS]);

/* The buttons the acts are on, for chord_turn's `live` */
uint32_t chord_keys(const chord *c);

/* An act's key as a mask, 0 for none */
uint32_t chord_key_of(const chord *c, chord_act a);

/* The pad as the chords see it at a turn: `held`, and, when SELECT was held
 * at the last turn (`was`: the pad as it was, not as this said), a hotkey's
 * key in `live` that went down since and is up again (`pressed`,
 * diatom_port_input_pressed), held for this one turn with SELECT and let go
 * at the next. A slow game polls ten times a second (Saturn, 2026-11-20), and
 * a quick press of R1 under a held SELECT fits between two polls: so it is
 * its hotkey once, even when SELECT was let go in the same moment. Nothing is
 * added with no SELECT held before: the key is the game's then, and a SELECT
 * pressed in that same turn may have come after it was let go. SELECT itself
 * is never added on its own: it is a game button too, and a tap of it between
 * two polls stays the game's as libretro polls it. The core sees the view's
 * buttons only through chord_turn's hide and give, and a key added here is
 * one it never had. Feed chord_turn the view, and its `was` the last view. */
uint32_t chord_view(chord *c, uint32_t held, uint32_t was, uint32_t pressed,
                    uint32_t live);

/* One turn. `held` is the pad now, `was` the pad at the last turn, `now_ms`
 * a clock in milliseconds, and `live` which of the acts' keys (chord_keys)
 * are hotkeys now (a game whose core keeps no state has no save or load, and
 * their keys are its own under SELECT). MENU is Diatom's and counts as no
 * button. */
chord_out chord_turn(chord *c, uint32_t held, uint32_t was, uint64_t now_ms,
                     uint32_t live);

#endif
