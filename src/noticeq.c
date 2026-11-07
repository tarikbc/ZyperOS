/* SPDX-License-Identifier: GPL-3.0-only */
/* The pop-ups' queue. See src/noticeq.h. */
#include "noticeq.h"

#include <string.h>

void nq_clear(noticeq *q)
{
	memset(q, 0, sizeof *q);
}

static void drop(noticeq *q, int i)
{
	memmove(&q->q[i], &q->q[i + 1], (size_t)(q->n - i - 1) * sizeof q->q[0]);
	q->n--;
}

void nq_push(noticeq *q, const nq_item *it)
{
	int i, at;

	if (it->kind == NQ_PROGRESS) {
		/* A newer count of one already waiting takes its place */
		for (i = 0; i < q->n; i++)
			if (q->q[i].kind == NQ_PROGRESS && q->q[i].id == it->id) {
				q->q[i] = *it;
				return;
			}
	} else if (it->id) {
		/* The unlock says what its progress was going to */
		for (i = q->n - 1; i >= 0; i--)
			if (q->q[i].kind == NQ_PROGRESS && q->q[i].id == it->id) drop(q, i);
	}
	if (q->n == NQ_MAX) {
		/* The oldest count; else the oldest that is not an alert */
		for (i = 0; i < q->n && q->q[i].kind != NQ_PROGRESS; i++) ;
		if (i == q->n)
			for (i = 0; i < q->n && q->q[i].kind == NQ_ALERT; i++) ;
		drop(q, i < q->n ? i : 0);
	}
	/* An alert after the alerts already waiting, ahead of the rest;
	 * progress at the end; anything else after the last that is not
	 * progress, ahead of the counts waiting */
	at = q->n;
	if (it->kind == NQ_ALERT)
		for (at = 0; at < q->n && q->q[at].kind == NQ_ALERT; at++) ;
	else if (it->kind != NQ_PROGRESS)
		for (at = 0; at < q->n && q->q[at].kind != NQ_PROGRESS; at++) ;
	memmove(&q->q[at + 1], &q->q[at], (size_t)(q->n - at) * sizeof q->q[0]);
	q->q[at] = *it;
	q->n++;
}

bool nq_next(noticeq *q, unsigned now, nq_item *out)
{
	if (!q->n || (int)(now - q->busy_until) < 0) return false;
	*out = q->q[0];
	drop(q, 0);
	q->busy_until = now + (unsigned)(out->ms > 0 ? out->ms : 0) + NQ_GAP_MS;
	return true;
}

void nq_hold(noticeq *q, unsigned now, int ms)
{
	unsigned until = now + (unsigned)(ms > 0 ? ms : 0) + NQ_GAP_MS;

	if ((int)(until - q->busy_until) > 0) q->busy_until = until;
}
