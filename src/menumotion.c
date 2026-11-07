/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#include "menumotion.h"
#include "design.h"

/* One open screen. A base has no panel and no frames of its own. */
typedef struct {
	bool     base;
	bool     rest;    /* a press put it there, or it never moves */
	bool     scrim;   /* the scrim arrives with it: opened on a base */
	unsigned t0;      /* when it opened, or was gone back to */
	unsigned lead;    /* how long the panel before it takes to leave first */
} level;

/* Level 0 is the shelf, a base, and is never closed. */
static level lv_at[MM_LEVELS] = { [0] = { .base = true, .rest = true } };
static int depth;

/* The panel left over a base by the last close onto it. */
static struct {
	bool     on;
	unsigned t0;
} leave;

static const mm_frame REST = { { 1.0f, 0.0f }, { 1.0f, 1.0f }, 1.0f, false };

static mm_pose arriving(float u)
{
	float e = ds_out(u);

	return (mm_pose){ MM_SCALE_FROM + (1.0f - MM_SCALE_FROM) * e, e };
}

static mm_pose leaving(float u)
{
	float e = ds_in(u);

	return (mm_pose){ 1.0f - (1.0f - MM_SCALE_FROM) * e, 1.0f - e };
}

void mm_reset(void)
{
	depth = 0;
}

int mm_depth(void)
{
	return depth;
}

/* The next level up, or 0 when there is no room for one. */
static int push(unsigned now)
{
	if (depth >= MM_LEVELS - 1) return 0;
	depth++;
	lv_at[depth] = (level){ .t0 = now };
	return depth;
}

int mm_open(unsigned now, bool shown)
{
	int lv = push(now);

	if (!lv) return 0;
	if (lv_at[lv - 1].base) {
		lv_at[lv].scrim = true;
		leave.on = false;
	} else if (shown) {
		lv_at[lv].lead = DS_MS_EXIT;
	}
	return lv;
}

int mm_base(unsigned now, bool shown)
{
	int lv = push(now);

	if (!lv) return 0;
	lv_at[lv].base = lv_at[lv].rest = true;
	leave.on = shown && !lv_at[lv - 1].base;
	leave.t0 = now;
	return lv;
}

/* Level `under` gone back to from level `from` or deeper: the panel on
 * screen leaves first, when it is one of theirs, and `under`'s arrives; when
 * `under`'s own is the one on screen, it never left. */
static void back_to(int under, int from, unsigned now, int shown)
{
	if (shown == under) {
		lv_at[under].rest = true;
		return;
	}
	lv_at[under] = (level){ .t0 = now, .lead = shown >= from ? DS_MS_EXIT : 0 };
}

void mm_close(int lv, unsigned now, int shown, bool all)
{
	level *under;

	if (lv <= 0 || lv > depth) return;
	depth = lv - 1;
	under = &lv_at[depth];

	if (lv_at[lv].base) {
		/* Whatever was leaving over it went with it */
		leave.on = false;
		if (!under->base && !all) *under = (level){ .scrim = true, .t0 = now };
		return;
	}
	if (under->base) {
		leave.on = shown >= lv;
		leave.t0 = now;
		return;
	}
	if (!all) back_to(depth, lv, now, shown);
}

void mm_return(int lv, unsigned now, int shown)
{
	if (lv <= 0 || lv > depth) return;
	depth = lv;
	if (!lv_at[lv].base) back_to(lv, lv + 1, now, shown);
}

void mm_press(int lv)
{
	if (lv > 0 && lv <= depth) lv_at[lv].rest = true;
}

int mm_settle(int lv, unsigned now, int shown)
{
	const level *l;
	bool waiting;

	if (lv <= 0 || lv > depth || lv_at[lv].base) return shown;
	l = &lv_at[lv];
	waiting = !l->rest && now - l->t0 < l->lead;
	mm_press(lv);
	return waiting ? lv : shown;
}

mm_frame mm_at(int lv, unsigned now)
{
	const level *l;
	mm_frame f = REST;
	unsigned u;

	if (lv <= 0 || lv > depth) return f;
	l = &lv_at[lv];
	if (l->base || l->rest) return f;

	u = now - l->t0;
	if (u < l->lead) {
		f.before = leaving((float)u / (float)l->lead);
		f.panel = (mm_pose){ MM_SCALE_FROM, 0.0f };
		f.moving = true;
		return f;
	}
	u -= l->lead;
	if (u >= DS_MS_QUICK) return f;
	f.panel = arriving((float)u / (float)DS_MS_QUICK);
	if (l->scrim) f.scrim = f.panel.alpha;
	f.moving = true;
	return f;
}

mm_frame mm_leaving(unsigned now)
{
	mm_frame f = { { 1.0f, 0.0f }, { 1.0f, 0.0f }, 0.0f, false };
	unsigned u = now - leave.t0;

	if (!leave.on || u >= DS_MS_EXIT) return f;
	f.before = leaving((float)u / (float)DS_MS_EXIT);
	f.scrim = f.before.alpha;
	f.moving = true;
	return f;
}

void mm_leaving_cut(void)
{
	leave.on = false;
}
