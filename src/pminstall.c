/* SPDX-License-Identifier: GPL-3.0-only */
/* What the Install PortMaster page says. See src/pminstall.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pminstall.h"

#include "i18n.h"

static const struct { const char *word; pm_state st; } STATES[] = {
	{ "checking", PM_CHECKING }, { "downloading", PM_DOWNLOADING },
	{ "verifying", PM_VERIFYING }, { "unpacking", PM_UNPACKING },
	{ "done", PM_DONE }, { "error", PM_ERROR },
};

static const struct { const char *word; pm_error err; } ERRORS[] = {
	{ "network", PM_ERR_NETWORK }, { "download", PM_ERR_DOWNLOAD },
	{ "checksum", PM_ERR_CHECKSUM }, { "space", PM_ERR_SPACE },
	{ "unpack", PM_ERR_UNPACK }, { "release", PM_ERR_RELEASE },
	{ "internal", PM_ERR_INTERNAL },
};

void pm_status_parse(const char *text, pm_status *out)
{
	const char *p = text;

	memset(out, 0, sizeof *out);
	out->state = PM_IDLE;
	while (p && *p) {
		const char *e = strchr(p, '\n');
		size_t n = e ? (size_t)(e - p) : strlen(p);
		char line[128], *val;
		size_t i;

		if (n >= sizeof line) n = sizeof line - 1;
		memcpy(line, p, n);
		line[n] = 0;
		p = e ? e + 1 : NULL;
		val = strchr(line, ' ');
		if (!val) continue;
		*val++ = 0;
		if (!strcmp(line, "state")) {
			for (i = 0; i < sizeof STATES / sizeof *STATES; i++)
				if (!strcmp(val, STATES[i].word)) out->state = STATES[i].st;
		} else if (!strcmp(line, "error")) {
			/* An error the page has no words for is still an error */
			out->error = PM_ERR_INTERNAL;
			for (i = 0; i < sizeof ERRORS / sizeof *ERRORS; i++)
				if (!strcmp(val, ERRORS[i].word)) out->error = ERRORS[i].err;
		} else if (!strcmp(line, "version")) {
			snprintf(out->version, sizeof out->version, "%s", val);
		} else if (!strcmp(line, "done")) {
			out->done = strtoull(val, NULL, 10);
		} else if (!strcmp(line, "total")) {
			out->total = strtoull(val, NULL, 10);
		} else if (!strcmp(line, "need")) {
			out->need = strtoull(val, NULL, 10);
		} else if (!strcmp(line, "time")) {
			out->time = strtoll(val, NULL, 10);
		}
	}
	if (out->state == PM_ERROR && out->error == PM_ERR_NONE) out->error = PM_ERR_INTERNAL;
}

static void key(su_page *p, const char *button, const char *label, su_action act)
{
	if (p->nkeys >= SU_MAX_KEYS) return;
	snprintf(p->keys[p->nkeys].button, sizeof p->keys[0].button, "%s", button);
	snprintf(p->keys[p->nkeys].label, sizeof p->keys[0].label, "%s", label);
	p->keys[p->nkeys].act = act;
	p->nkeys++;
}

static void note(char notes[PM_NOTES][120], int *n, const char *text)
{
	if (*n < PM_NOTES) snprintf(notes[(*n)++], 120, "%s", text);
}

/* "6.1 of 24 MB": the unit once, the larger one's (src/sysupdate.c's rule) */
static void of_size(unsigned long long done, unsigned long long total, char *buf, size_t n)
{
	char t[24], d[24];

	su_size(total, t, sizeof t);
	if (total >= 1000ull * 1000) {
		double x = (double)done / 1e6;
		snprintf(d, sizeof d, x >= 10 ? "%.0f" : "%.1f", x);
	} else {
		snprintf(d, sizeof d, "%llu", (done + 500) / 1000);
	}
	snprintf(buf, n, T(S_UPDATE_OF_SIZE), d, t);
}

static float frac(unsigned long long a, unsigned long long b)
{
	if (!b) return 0.0f;
	if (a >= b) return 1.0f;
	return (float)((double)a / (double)b);
}

/* Install, or Try Again: refused, with the reason where the bar goes, with
 * no way out to the internet */
static void install_key(su_page *p, const pm_view *v, const char *label)
{
	if (!v->network)
		snprintf(p->line, sizeof p->line, "%s", T(S_UPDATE_NEED_WIFI_DOWNLOAD));
	else
		key(p, "A", label, SUA_DOWNLOAD);
}

static void error_words(const pm_status *st, char *buf, size_t n)
{
	char need[24];

	switch (st->error) {
	case PM_ERR_NETWORK:  snprintf(buf, n, "%s", T(S_PORTMASTER_ERR_NETWORK)); break;
	case PM_ERR_DOWNLOAD: snprintf(buf, n, "%s", T(S_UPDATE_DOWNLOAD_FAILED)); break;
	case PM_ERR_CHECKSUM: snprintf(buf, n, "%s", T(S_PORTMASTER_ERR_CHECKSUM)); break;
	case PM_ERR_SPACE:
		su_size(st->need, need, sizeof need);
		snprintf(buf, n, T(S_PORTMASTER_ERR_SPACE), need);
		break;
	case PM_ERR_UNPACK:   snprintf(buf, n, "%s", T(S_PORTMASTER_ERR_UNPACK)); break;
	case PM_ERR_RELEASE:  snprintf(buf, n, "%s", T(S_PORTMASTER_ERR_RELEASE)); break;
	default:              snprintf(buf, n, "%s", T(S_PORTMASTER_ERR_INTERNAL)); break;
	}
}

void pm_page_build(const pm_view *v, su_page *p, char notes[PM_NOTES][120], int *nnotes)
{
	pm_state s = v->st.state;

	memset(p, 0, sizeof *p);
	*nnotes = 0;
	snprintf(p->from, sizeof p->from, "%s", "PortMaster");
	/* The installer's word stands while it runs; once it is gone, the card
	 * does. A run cut short (killed, the handheld turned off) said nothing
	 * more, and is a stop. PortMaster on the card is done whatever the file
	 * says: put there by hand, or by a run before this start. */
	if (v->running && s == PM_IDLE) s = PM_CHECKING;
	if (!v->running) {
		if (v->installed) s = PM_DONE;
		else if (s == PM_DONE) s = PM_IDLE;
	}
	if (!v->python && !v->installed && !v->running) {
		/* A system from before PortMaster had Python to run on: nothing to
		 * offer until the system is updated */
		snprintf(p->sub, sizeof p->sub, "%s", T(S_PORTMASTER_NOT_INSTALLED));
		snprintf(p->warn, sizeof p->warn, "%s", T(S_PORTMASTER_NEEDS_SYSTEM));
		key(p, "B", T(S_UPDATE_BACK), SUA_BACK);
		return;
	}

	/* What PortMaster is, under the line, from the offer until it is in:
	 * the page is never a bar alone */
	if (s != PM_DONE && s != PM_ERROR) {
		p->notes = true;
		note(notes, nnotes, T(S_PORTMASTER_POINT_STORE));
		note(notes, nnotes, T(S_PORTMASTER_POINT_FILES));
	}
	switch (s) {
	case PM_IDLE:
		snprintf(p->sub, sizeof p->sub, "%s", T(S_PORTMASTER_FROM));
		install_key(p, v, T(S_PORTMASTER_INSTALL));
		if (!p->line[0]) snprintf(p->line, sizeof p->line, "%s", T(S_PORTMASTER_FROM_GITHUB));
		break;
	case PM_CHECKING:
		snprintf(p->sub, sizeof p->sub, "%s", T(S_PORTMASTER_LOOKING));
		key(p, "B", T(S_UPDATE_BACK_GOES_ON), SUA_BACK);
		return;
	case PM_DOWNLOADING:
	case PM_VERIFYING:
	case PM_UNPACKING:
		if (!v->running) {
			/* It stopped mid-way and said nothing more: the problem
			 * where the points were */
			p->notes = false;
			*nnotes = 0;
			snprintf(p->sub, sizeof p->sub, "%s", T(S_PORTMASTER_NOT_INSTALLED));
			snprintf(p->warn, sizeof p->warn, "%s", T(S_PORTMASTER_ERR_STOPPED));
			install_key(p, v, T(S_UPDATE_TRY_AGAIN));
			break;
		}
		if (v->st.version[0]) snprintf(p->sub, sizeof p->sub, T(S_PORTMASTER_VERSION), v->st.version);
		else snprintf(p->sub, sizeof p->sub, "%s", T(S_PORTMASTER_FROM));
		p->bar = true;
		if (s == PM_VERIFYING) {
			p->frac = 1.0f;
			snprintf(p->bar_left, sizeof p->bar_left, "%s", T(S_PORTMASTER_CHECKING));
		} else {
			p->frac = frac(v->st.done, v->st.total);
			snprintf(p->bar_left, sizeof p->bar_left, "%s",
			         T(s == PM_UNPACKING ? S_PORTMASTER_UNPACKING : S_UPDATE_DOWNLOADING));
			if (v->st.total) of_size(v->st.done, v->st.total, p->bar_right, sizeof p->bar_right);
		}
		key(p, "B", T(S_UPDATE_BACK_GOES_ON), SUA_BACK);
		return;
	case PM_DONE:
		if (v->st.version[0]) snprintf(p->sub, sizeof p->sub, T(S_PORTMASTER_INSTALLED), v->st.version);
		else snprintf(p->sub, sizeof p->sub, "%s", T(S_PORTMASTER_FROM));
		p->notes = true;
		note(notes, nnotes, T(S_PORTMASTER_DONE_SHELF));
		note(notes, nnotes, T(S_PORTMASTER_DONE_OPEN));
		key(p, "A", T(S_PORTMASTER_OPEN), SUA_OPEN);
		break;
	case PM_ERROR:
		snprintf(p->sub, sizeof p->sub, "%s", T(S_PORTMASTER_NOT_INSTALLED));
		error_words(&v->st, p->warn, sizeof p->warn);
		install_key(p, v, T(S_UPDATE_TRY_AGAIN));
		break;
	}
	key(p, "B", T(S_UPDATE_BACK), SUA_BACK);
}

bool pm_listing_has(const game_list *l)
{
	int i;

	for (i = 0; i < l->count; i++)
		if (!strcasecmp(l->items[i].file, PM_SCRIPT)) return true;
	return false;
}

bool pm_listing_add(game_list *l)
{
	game_entry *more, *e;

	if (pm_listing_has(l)) return true;
	more = realloc(l->items, (size_t)(l->count + 1) * sizeof *more);
	if (!more) return false;
	l->items = more;
	e = &more[l->count];
	memset(e, 0, sizeof *e);
	/* As lib_scan_listing makes it from the file: the name its stem, the
	 * title left for lib_scan_finish */
	snprintf(e->file, sizeof e->file, "%s", PM_SCRIPT);
	snprintf(e->name, sizeof e->name, "%s", PM_FOLDER);
	e->added = LIB_ADDED_UNKNOWN;
	l->count++;
	l->scanned = true;
	return true;
}
