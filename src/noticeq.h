/* SPDX-License-Identifier: GPL-3.0-only */
/* The pop-ups over a game, one after another.
 *
 * Diatom holds one overlay: a second OVERLAY replaces the first at once. Two
 * achievements unlocked a frame apart - common, a level's end often fires two
 * - showed the first for a frame and the second for four seconds. So the
 * launcher keeps them in a queue and hands Diatom the next only when the one
 * on screen has had its time.
 *
 * An alert ("Getting hot", src/heat.h) goes ahead of everything waiting: it
 * is about the handheld, now, and an achievement keeps. Then unlocks, the
 * set's arrival and mastery, in the order they came; progress ("74 / 100")
 * waits behind them, and a newer count of the same
 * achievement replaces one still waiting rather than queueing behind it. An
 * unlock drops its own progress still waiting: the unlock says it.
 *
 * Pure - no SDL, no clock of its own, the caller's milliseconds - so
 * tools/noticeq-check.c holds it on a build machine. main.c renders an item
 * with src/notice.c and sends it. */
#ifndef ZYPEROS_NOTICEQ_H
#define ZYPEROS_NOTICEQ_H

#include <stdbool.h>

typedef enum {
	NQ_ALERT,      /* the handheld itself: "Getting hot" */
	NQ_UNLOCK,     /* an achievement earned, and mastery */
	NQ_SET,        /* the set arrived, or "3 of 36" at the start */
	NQ_PROGRESS,   /* a count moving */
} nq_kind;

#define NQ_MAX   8
/* Between one pop-up going and the next coming, so the change reads as a new
 * one and not as the text flickering */
#define NQ_GAP_MS 300

typedef struct {
	nq_kind kind;
	int     id;            /* the achievement, 0 for none */
	int     ms;            /* how long it is shown */
	char    head[96];      /* the small line */
	char    body[160];     /* the large line */
	char    badge[512];    /* a badge PNG to draw beside, "" for none */
	int     value, target; /* progress, for the strip; target 0 for none */
	int     mark;          /* a mark drawn where a badge goes: notice.h's
	                        * NOTICE_MARK_*, 0 for none */
} nq_item;

typedef struct {
	nq_item  q[NQ_MAX];
	int      n;
	unsigned busy_until;   /* nothing new before this */
} noticeq;

void nq_clear(noticeq *q);
/* Queued by the rules above. When full, the oldest progress goes first, then
 * the oldest that is not an alert, then the oldest of anything. */
void nq_push(noticeq *q, const nq_item *it);
/* The next to show at `now`, taken from the queue, or false while one is on
 * screen or none waits. Marks the queue busy for its time and the gap. */
bool nq_next(noticeq *q, unsigned now, nq_item *out);
/* Something shown past the queue - fast forward's word - keeps the next one
 * back until it has gone */
void nq_hold(noticeq *q, unsigned now, int ms);

#endif
