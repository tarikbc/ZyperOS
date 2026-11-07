/* The pop-ups' queue over a game (src/noticeq.h): one at a time, each for its
 * time; unlocks ahead of progress; a newer count replacing a waiting one; an
 * unlock taking its own progress away; and a full queue giving up progress
 * first. Links src/noticeq.c and nothing else. */
#include "../src/noticeq.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

static nq_item item(nq_kind k, int id, int ms, const char *body)
{
	nq_item it;

	memset(&it, 0, sizeof it);
	it.kind = k;
	it.id = id;
	it.ms = ms;
	snprintf(it.body, sizeof it.body, "%s", body);
	return it;
}

int main(void)
{
	noticeq q;
	nq_item it, got;
	int i;

	printf("one at a time:\n");
	nq_clear(&q);
	ck(!nq_next(&q, 0, &got), "nothing to show in an empty queue");
	it = item(NQ_UNLOCK, 1, 4000, "first"); nq_push(&q, &it);
	it = item(NQ_UNLOCK, 2, 4000, "second"); nq_push(&q, &it);
	ck(nq_next(&q, 1000, &got) && !strcmp(got.body, "first"), "the first unlock at once");
	ck(!nq_next(&q, 3000, &got), "not the second over the first");
	ck(!nq_next(&q, 1000 + 4000, &got), "nor the moment the first ends: a gap");
	ck(nq_next(&q, 1000 + 4000 + NQ_GAP_MS, &got) && !strcmp(got.body, "second"),
	   "the second after the first's time and the gap");

	printf("unlocks ahead of progress:\n");
	nq_clear(&q);
	it = item(NQ_PROGRESS, 7, 2500, "count"); nq_push(&q, &it);
	it = item(NQ_UNLOCK, 8, 4000, "unlock"); nq_push(&q, &it);
	ck(nq_next(&q, 0, &got) && !strcmp(got.body, "unlock"), "an unlock jumps a waiting count");
	ck(nq_next(&q, 10000, &got) && !strcmp(got.body, "count"), "the count after it");

	printf("an alert ahead of everything waiting:\n");
	nq_clear(&q);
	it = item(NQ_UNLOCK, 1, 4000, "on screen"); nq_push(&q, &it);
	nq_next(&q, 0, &got);
	it = item(NQ_PROGRESS, 7, 2500, "count"); nq_push(&q, &it);
	it = item(NQ_UNLOCK, 8, 4000, "unlock"); nq_push(&q, &it);
	it = item(NQ_ALERT, 0, 4000, "hot"); nq_push(&q, &it);
	it = item(NQ_ALERT, 0, 4000, "hot again"); nq_push(&q, &it);
	ck(!nq_next(&q, 1000, &got), "an alert does not cut the one on screen short");
	ck(nq_next(&q, 4000 + NQ_GAP_MS, &got) && !strcmp(got.body, "hot"), "the alert next");
	ck(nq_next(&q, 20000, &got) && !strcmp(got.body, "hot again"), "alerts in their order");
	ck(nq_next(&q, 40000, &got) && !strcmp(got.body, "unlock"), "then the unlock");
	ck(nq_next(&q, 60000, &got) && !strcmp(got.body, "count"), "then the count");
	nq_clear(&q);
	it = item(NQ_ALERT, 0, 4000, "hot"); nq_push(&q, &it);
	for (i = 0; i < NQ_MAX + 3; i++) { it = item(NQ_UNLOCK, 100 + i, 4000, "u"); nq_push(&q, &it); }
	ck(q.n == NQ_MAX && q.q[0].kind == NQ_ALERT, "a full queue of unlocks keeps the alert");

	printf("a newer count replaces the waiting one:\n");
	nq_clear(&q);
	it = item(NQ_UNLOCK, 1, 4000, "busy"); nq_push(&q, &it);
	nq_next(&q, 0, &got);
	it = item(NQ_PROGRESS, 7, 2500, "3/10"); nq_push(&q, &it);
	it = item(NQ_PROGRESS, 7, 2500, "5/10"); nq_push(&q, &it);
	ck(q.n == 1, "one count waiting, not two");
	ck(nq_next(&q, 10000, &got) && !strcmp(got.body, "5/10"), "and it is the newest");

	printf("an unlock takes its own progress away:\n");
	nq_clear(&q);
	it = item(NQ_UNLOCK, 1, 4000, "busy"); nq_push(&q, &it);
	nq_next(&q, 0, &got);
	it = item(NQ_PROGRESS, 7, 2500, "9/10"); nq_push(&q, &it);
	it = item(NQ_UNLOCK, 7, 4000, "done"); nq_push(&q, &it);
	ck(q.n == 1 && !strcmp(q.q[0].body, "done"), "9/10 is gone, the unlock stays");

	printf("a full queue:\n");
	nq_clear(&q);
	it = item(NQ_PROGRESS, 50, 2500, "count"); nq_push(&q, &it);
	for (i = 0; i < NQ_MAX - 1; i++) { it = item(NQ_UNLOCK, 100 + i, 4000, "u"); nq_push(&q, &it); }
	ck(q.n == NQ_MAX, "holds NQ_MAX");
	it = item(NQ_UNLOCK, 999, 4000, "late"); nq_push(&q, &it);
	ck(q.n == NQ_MAX, "and no more");
	for (i = 0; i < q.n; i++) ck(q.q[i].kind != NQ_PROGRESS, "the count went first");
	ck(!strcmp(q.q[q.n - 1].body, "late"), "and the late unlock is kept");

	printf("something shown past the queue:\n");
	nq_clear(&q);
	nq_hold(&q, 0, 1500);
	it = item(NQ_UNLOCK, 1, 4000, "after"); nq_push(&q, &it);
	ck(!nq_next(&q, 1000, &got), "waits for fast forward's word to go");
	ck(nq_next(&q, 1500 + NQ_GAP_MS, &got), "and comes after it");
	if (fails) { printf("noticeq-check: %d FAILED\n", fails); return 1; }
	printf("noticeq-check: ok\n");
	return 0;
}
