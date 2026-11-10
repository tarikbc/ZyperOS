/* SPDX-License-Identifier: GPL-3.0-only */
/* See clients.h. */
#include "clients.h"

#include <stdio.h>
#include <string.h>

void clients_clear(clients *t) { memset(t, 0, sizeof *t); }

void clients_note(clients *t, const char *addr, const char *key, unsigned now)
{
	int i, free_at = -1, oldest = 0;

	if (!addr || !*addr) return;
	if (!key) key = "";
	for (i = 0; i < CLIENTS_MAX; i++) {
		client_seen *c = &t->c[i];

		if (!c->used) { if (free_at < 0) free_at = i; continue; }
		if (!strcmp(c->addr, addr) && !strncmp(c->key, key, CLIENTS_KEY - 1)) {
			c->last = now;
			return;
		}
		if (now - c->last > now - t->c[oldest].last) oldest = i;
	}
	i = free_at >= 0 ? free_at : oldest;
	memset(&t->c[i], 0, sizeof t->c[i]);
	snprintf(t->c[i].addr, sizeof t->c[i].addr, "%s", addr);
	snprintf(t->c[i].key, sizeof t->c[i].key, "%s", key);
	t->c[i].last = now;
	t->c[i].used = true;
}

static bool is_open(const char *addr, const char *const *open, int nopen)
{
	int i;

	for (i = 0; i < nopen; i++)
		if (open[i] && !strcmp(open[i], addr)) return true;
	return false;
}

int clients_count(const clients *t, const char *const *open, int nopen, unsigned now)
{
	int n = 0, i, j;

	/* Each session that is alive: asked lately, or its address has a
	 * connection open */
	for (i = 0; i < CLIENTS_MAX; i++) {
		const client_seen *c = &t->c[i];

		if (c->used && c->key[0] &&
		    (now - c->last < CLIENTS_WINDOW_MS || is_open(c->addr, open, nopen)))
			n++;
	}
	/* And each address alive with no live session of its own: a browser
	 * on the PIN page, or one whose connection has not asked anything yet */
	for (i = 0; i < CLIENTS_MAX; i++) {
		const client_seen *c = &t->c[i];
		bool alive, has_session = false, counted = false;

		if (!c->used || c->key[0]) continue;
		alive = now - c->last < CLIENTS_WINDOW_MS || is_open(c->addr, open, nopen);
		if (!alive) continue;
		for (j = 0; j < CLIENTS_MAX; j++) {
			const client_seen *d = &t->c[j];

			if (d->used && d->key[0] && !strcmp(d->addr, c->addr) &&
			    (now - d->last < CLIENTS_WINDOW_MS || is_open(d->addr, open, nopen)))
				has_session = true;
		}
		for (j = 0; j < i; j++)
			if (t->c[j].used && !t->c[j].key[0] && !strcmp(t->c[j].addr, c->addr)) counted = true;
		if (!has_session && !counted) n++;
	}
	/* And an address with a connection open that has asked nothing yet */
	for (i = 0; i < nopen; i++) {
		bool known = false;

		if (!open[i]) continue;
		for (j = 0; j < CLIENTS_MAX && !known; j++)
			if (t->c[j].used && !strcmp(t->c[j].addr, open[i])) known = true;
		for (j = 0; j < i && !known; j++)
			if (open[j] && !strcmp(open[j], open[i])) known = true;
		if (!known) n++;
	}
	return n;
}
