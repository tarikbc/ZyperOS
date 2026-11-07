/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* See snapshelf.h. */
#include <stdio.h>
#include <string.h>

#include "snapshelf.h"

#include "design.h"
#include "snaps.h"

/* DESIGN.md 8.1's turn curve, a symmetric ease for a change of state */
static float turn(float u)
{
	if (u <= 0) return 0;
	if (u >= 1) return 1;
	return u < .5f ? 4 * u * u * u : 1 - (-2 * u + 2) * (-2 * u + 2) * (-2 * u + 2) / 2;
}

static void go(snapshelf *s, sns_phase p, uint32_t now)
{
	s->phase = p;
	s->t = now;
}

const char *snapshelf_step(snapshelf *s, const char *path, bool front,
                           bool picture, uint32_t now, bool *hold, float *k)
{
	float turned = 0;
	bool held = false;

	if (!path) path = "";
	/* The focus moved: whatever was showing went with its card */
	if (strcmp(path, s->key) != 0) {
		snprintf(s->key, sizeof s->key, "%s", path);
		go(s, path[0] ? SNS_WAIT : SNS_IDLE, now);
	}
	/* Not in front: a menu, a dark screen, a move in flight */
	if (!front) {
		if (s->phase == SNS_IN || s->phase == SNS_PLAY) {
			s->from = s->phase == SNS_PLAY ? 1.f
			        : turn((float)(now - s->t) / DS_MS_TURN);
			go(s, SNS_OUT, now);
		} else if (s->phase == SNS_WAIT || s->phase == SNS_ASKED) {
			go(s, SNS_IDLE, now);
		}
	} else if (s->phase == SNS_IDLE && s->key[0]) {
		/* In front again on the same game: the wait starts over */
		go(s, SNS_WAIT, now);
	}

	switch (s->phase) {
	case SNS_WAIT:
		/* The bar full: the snap goes, and if its first picture came while
		 * it was held, the cover starts to turn this same frame */
		if (now - s->t >= SNAPS_WAIT_MS) {
			go(s, picture ? SNS_IN : SNS_ASKED, now);
		} else if (now - s->t >= SNAPS_ASK_MS) {
			held = true;
		}
		break;
	case SNS_ASKED:
		if (picture) go(s, SNS_IN, now);
		break;
	case SNS_IN:
		turned = turn((float)(now - s->t) / DS_MS_TURN);
		if (now - s->t >= DS_MS_TURN) { go(s, SNS_PLAY, now); turned = 1; }
		break;
	case SNS_PLAY:
		turned = 1;
		break;
	case SNS_OUT:
		turned = s->from * (1 - (float)(now - s->t) / DS_MS_QUICK);
		if (now - s->t >= DS_MS_QUICK || turned <= 0) { go(s, SNS_IDLE, now); turned = 0; }
		break;
	default:
		break;
	}
	if (k) *k = turned;
	if (hold) *hold = held;
	return held || s->phase == SNS_ASKED || s->phase == SNS_IN || s->phase == SNS_PLAY ? s->key : "";
}

bool snapshelf_moving(const snapshelf *s)
{
	return s->phase == SNS_IN || s->phase == SNS_OUT;
}
