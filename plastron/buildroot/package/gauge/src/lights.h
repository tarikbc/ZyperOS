/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * What the side lights show, as pure functions: no files, no clock of its
 * own, so check.c runs every rule on the host. gauge.c reads the battery, the
 * setting and the launcher's lines, and shows what lights_frame says.
 *
 * Bits: bit 0 is battery0 (the bottom light's red), bit 1 battery1 (its
 * green), bits 2 to 4 the three green lights above it, bottom to top.
 */
#ifndef GAUGE_LIGHTS_H
#define GAUGE_LIGHTS_H

#define L_RED    (1 << 0)
#define L_GREEN  (1 << 1)
#define L_ALL    (L_GREEN | (L_GREEN << 1) | (L_GREEN << 2) | (L_GREEN << 3))

#define LOW          10     /* under this, off the cable, the red flash */
#define VERY_LOW     5      /* under this, the red flash comes twice as often */
#define IDLE_MS      30000  /* nothing moving: look at the battery this often */
#define MOVE_HOLD_MS 1500   /* files moving: the wave stays this long after the
                             * last "move" line, so a sender may look once a
                             * second and the wave never stops between */

/* MENU > Side Lights: which parts show. The setting file holds one of the
 * four words older gauges know first (auto, charging, low, off: the bar and
 * the red flash), then a word for each new part, "level" to show it or
 * "-level" not to; a part the file does not name is on after "auto" and off
 * after the other three. So an older gauge reads the battery parts right, and
 * a card that never had the file, or still has today's bare "auto", gets
 * every part. */
struct lights_cfg {
	int bar;     /* the charging bar, on the cable */
	int low;     /* the red flash, under LOW off the cable */
	int level;   /* the volume or brightness while it changes */
	int done;    /* files and covers: the wave while files move to or from
	              * the card, a climb when files or covers finish */
	int cheer;   /* a sparkle when an achievement unlocks */
};

/* The patterns that come and go (lights_frame shows the newest) */
enum { LE_NONE, LE_LEVEL, LE_DONE, LE_CHEER };

/* The way files move, for the wave */
enum { MV_IN, MV_OUT };

struct lights_state {
	/* The battery, as gauge.c last read it */
	int  pct;
	int  plugged;
	int  held;        /* on the cable, the charger holding it at its limit */
	long bar_at;      /* when the cable came or went: the bar starts again */
	long low_at;      /* when it fell under LOW or VERY_LOW: the flash's phase */
	int  known;       /* read at least once */

	/* The launcher's: the newest pattern */
	int  ev;
	long ev_at;       /* when the pattern began */
	long ev_last;     /* LE_LEVEL: the last change, which it holds after */
	int  ev_val, ev_max, ev_vol;

	/* Files moving to and from the card ("move IN OUT"): a layer of its
	 * own, under the patterns above, for as long as the lines come */
	int  mv_on;
	unsigned long long mv_in, mv_out;   /* bytes a second each way, as last said */
	long mv_last;     /* the last line */
	long mv_next;     /* when the wave steps next */
	int  mv_step;     /* the step of the run showing, 0 to 7 */
	int  mv_way;      /* MV_IN or MV_OUT: the run's, chosen as it begins */

	long asked_at;    /* L2+R2 (SIGUSR1), or -1 */
};

void lights_parse(struct lights_cfg *c, const char *text);
void lights_init(struct lights_state *s);

/* The battery now. Moves the bar's start when the cable comes or goes, and
 * the red flash's when the level falls under LOW or VERY_LOW. */
void lights_battery(struct lights_state *s, int pct, int plugged, int held, long now);

/* One line from the launcher, without its newline:
 *   "level volume V MAX", "level brightness V MAX"   while it changes
 *   "done"                                           files or covers finished
 *   "cheer"                                          an achievement unlocked
 *   "move IN OUT"                                    files moving to (IN) and
 *                                                    from (OUT) the card, bytes
 *                                                    a second each way, while
 *                                                    they move; "move 0 0" when
 *                                                    they stop
 * Anything else is left alone, so a newer launcher can say more. A part the
 * setting turns off is not shown, and not kept for later. */
void lights_line(struct lights_state *s, const struct lights_cfg *c, const char *line, long now);

/* L2+R2: the level, flashed, over everything */
void lights_ask(struct lights_state *s, long now);

/* The lights at `now`, and in *wait the ms until they could change (IDLE_MS
 * when nothing moves). Clears a pattern that is over. */
int lights_frame(const struct lights_cfg *c, struct lights_state *s, long now, int *wait);

/* The level as lights, for L2+R2 */
int lights_level_bits(int pct);

#endif
