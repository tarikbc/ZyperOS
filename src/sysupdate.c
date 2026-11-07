/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* What MENU > System Update says. See src/sysupdate.h, and the update lab
 * (docs/design/mock/update-lab.html) for the page this lays out. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sysupdate.h"

#include "i18n.h"

static const struct { const char *word; su_state st; } STATES[] = {
	{ "idle", SU_IDLE }, { "unsupported", SU_UNSUPPORTED }, { "checking", SU_CHECKING },
	{ "uptodate", SU_UPTODATE }, { "available", SU_AVAILABLE },
	{ "needs-system", SU_NEEDS_SYSTEM }, { "copying", SU_COPYING },
	{ "downloading", SU_DOWNLOADING }, { "verifying", SU_VERIFYING },
	{ "installing", SU_INSTALLING },
	{ "ready", SU_READY }, { "stopped", SU_STOPPED }, { "error", SU_ERROR },
	{ "battery", SU_BATTERY }, { "space", SU_SPACE }, { "rollback", SU_ROLLBACK },
	{ "too-big", SU_TOO_BIG },
};

void su_status_parse(const char *text, su_status *out)
{
	const char *p = text;

	memset(out, 0, sizeof *out);
	out->state = SU_IDLE;
	while (p && *p) {
		const char *e = strchr(p, '\n');
		size_t n = e ? (size_t)(e - p) : strlen(p);
		char line[200], *val;

		if (n >= sizeof line) n = sizeof line - 1;
		memcpy(line, p, n);
		line[n] = 0;
		p = e ? e + 1 : NULL;
		val = strchr(line, ' ');
		if (!val) continue;
		*val++ = 0;
		if (!strcmp(line, "state")) {
			size_t i;
			for (i = 0; i < sizeof STATES / sizeof *STATES; i++)
				if (!strcmp(val, STATES[i].word)) out->state = STATES[i].st;
		} else if (!strcmp(line, "installed")) {
			snprintf(out->installed, sizeof out->installed, "%s", val);
		} else if (!strcmp(line, "version")) {
			snprintf(out->version, sizeof out->version, "%s", val);
		} else if (!strcmp(line, "size")) {
			out->size = strtoull(val, NULL, 10);
		} else if (!strcmp(line, "download")) {
			out->download = strtoull(val, NULL, 10);
		} else if (!strcmp(line, "done")) {
			out->done = strtoull(val, NULL, 10);
		} else if (!strcmp(line, "total")) {
			out->total = strtoull(val, NULL, 10);
		} else if (!strcmp(line, "files")) {
			sscanf(val, "%d %d", &out->files_done, &out->files_total);
		} else if (!strcmp(line, "kind")) {
			out->system = !strcmp(val, "system");
		} else if (!strcmp(line, "other")) {
			snprintf(out->other, sizeof out->other, "%s", val);
		} else if (!strcmp(line, "message")) {
			snprintf(out->message, sizeof out->message, "%s", val);
		} else if (!strcmp(line, "time")) {
			out->time = strtoll(val, NULL, 10);
		}
	}
}

int su_version_cmp(const char *a, const char *b)
{
	int i;
	for (i = 0; i < 3; i++) {
		char *ea, *eb;
		unsigned long x = strtoul(a, &ea, 10), y = strtoul(b, &eb, 10);
		if (x != y) return x < y ? -1 : 1;
		a = ea; b = eb;
		if (*a == '.') a++;
		if (*b == '.') b++;
	}
	if (*a == '-' && *b != '-') return -1;
	if (*b == '-' && *a != '-') return 1;
	return strcmp(a, b) < 0 ? -1 : strcmp(a, b) > 0;
}

void su_size(unsigned long long b, char *buf, size_t n)
{
	if (b >= 10ull * 1000 * 1000)  snprintf(buf, n, "%llu MB", (b + 500000) / 1000000);
	else if (b >= 1000ull * 1000)  snprintf(buf, n, "%.1f MB", (double)b / 1e6);
	else if (b >= 1000)            snprintf(buf, n, "%llu KB", (b + 500) / 1000);
	else                           snprintf(buf, n, T(S_UPDATE_BYTES), b);
}

/* "6.1 of 14 MB": the unit once, the larger one's */
static void of_size(unsigned long long done, unsigned long long total, char *buf, size_t n)
{
	char t[24], d[24];
	su_size(total, t, sizeof t);
	if (total >= 1000ull * 1000) {
		double x = (double)done / 1e6;
		if (x >= 10) snprintf(d, sizeof d, "%.0f", x);
		else snprintf(d, sizeof d, "%.1f", x);
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

static void key(su_page *p, const char *button, const char *label, su_action act)
{
	if (p->nkeys >= SU_MAX_KEYS) return;
	snprintf(p->keys[p->nkeys].button, sizeof p->keys[0].button, "%s", button);
	snprintf(p->keys[p->nkeys].label, sizeof p->keys[0].label, "%s", label);
	p->keys[p->nkeys].act = act;
	p->nkeys++;
}

/* "14 MB to download · 168 MB in all" */
static void size_line(const su_view *v, char *buf, size_t n)
{
	char d[24], t[24];
	su_size(v->st.download > 1 ? v->st.download : 0, d, sizeof d);
	if (v->st.system)
		/* The system's root and kernel, and with them its ZyperOS */
		snprintf(buf, n, T(S_UPDATE_SIZE_SYSTEM), d);
	else if (v->st.size) {
		su_size(v->st.size, t, sizeof t);
		snprintf(buf, n, T(S_UPDATE_SIZE_BOTH), d, t);
	} else {
		snprintf(buf, n, T(S_UPDATE_SIZE_DOWNLOAD), d);
	}
}

/* Download (or Continue): refused, with the reason where the bar goes, when
 * it cannot work */
static void download_key(su_page *p, const su_view *v, const char *label)
{
	if (!v->wifi) {
		snprintf(p->line, sizeof p->line, "%s", T(S_UPDATE_NEED_WIFI_DOWNLOAD));
	} else if (v->battery < SU_MIN_BATTERY && !v->charging) {
		snprintf(p->warn, sizeof p->warn,
		         T(S_UPDATE_BATTERY_LOW), v->battery);
	} else {
		key(p, "A", label, SUA_DOWNLOAD);
	}
}

static void rollback_key(su_page *p, const su_view *v)
{
	char l[sizeof p->keys[0].label];
	if (!v->prev[0]) return;
	snprintf(l, sizeof l, T(S_UPDATE_ROLL_BACK_TO), v->prev);
	key(p, "Y", l, SUA_ROLLBACK);
}

/* The state as it stands now: the status file is the program's last word,
 * and the switch at the next start changes the folder under it. A status
 * about the installed version or an older one is done (a "ready 0.2.0"
 * left from before the restart that installed 0.2.0); a step back is done
 * once the folder is the version it went back to. */
static su_state effective(const su_view *v)
{
	su_state s = v->st.state;
	int c;

	if (v->running || !v->st.version[0] || !v->installed[0]) return s;
	c = su_version_cmp(v->st.version, v->installed);
	if (s == SU_ROLLBACK) return c == 0 ? SU_UPTODATE : s;
	if (c <= 0 && s != SU_UPTODATE && s != SU_UNSUPPORTED && s != SU_IDLE &&
	    s != SU_CHECKING && s != SU_ERROR)
		return SU_UPTODATE;
	return s;
}

void su_page_build(const su_view *v, su_page *p)
{
	su_state s = effective(v);
	bool running = v->running, went_back;

	memset(p, 0, sizeof *p);
	snprintf(p->from, sizeof p->from, "%s", v->installed[0] ? v->installed : "?");
	if (s == SU_UNSUPPORTED) {
		snprintf(p->sub, sizeof p->sub, "%s", T(S_UPDATE_UNSUPPORTED));
		snprintf(p->line, sizeof p->line, "%s", T(S_UPDATE_USE_INSTALLER));
		key(p, "B", T(S_UPDATE_BACK), SUA_BACK);
		return;
	}
	if (running && s != SU_COPYING && s != SU_DOWNLOADING && s != SU_VERIFYING && s != SU_INSTALLING) s = SU_CHECKING;
	if (!running && (s == SU_CHECKING || s == SU_COPYING || s == SU_DOWNLOADING || s == SU_VERIFYING ||
	                 s == SU_INSTALLING))
		s = SU_STOPPED;   /* it was cut short and did not say */
	/* A version that did not start went back by itself (update/apply.sh) */
	went_back = !running && !strncmp(v->result, "failed ", 7) && s != SU_READY &&
	            (!v->st.version[0] || !strcmp(v->st.version, v->result + 7));
	if (v->st.version[0] && s != SU_UPTODATE && s != SU_IDLE && s != SU_CHECKING)
		snprintf(p->to, sizeof p->to, "%s", v->st.version);

	switch (s) {
	case SU_IDLE:
		if (v->wifi) {
			snprintf(p->sub, sizeof p->sub, "%s", T(S_UPDATE_NOT_LOOKED));
			key(p, "A", T(S_UPDATE_LOOK), SUA_CHECK);
		} else {
			snprintf(p->sub, sizeof p->sub, "%s", T(S_UPDATE_NEED_WIFI_LOOK));
		}
		break;
	case SU_CHECKING:
		snprintf(p->sub, sizeof p->sub, "%s", T(S_UPDATE_LOOKING));
		break;
	case SU_UPTODATE: {
		/* Just switched (update/apply.sh's result): say from what */
		char a[32] = "", b[32] = "";
		if (sscanf(v->result, "updated %31s %31s", a, b) == 2 && !strcmp(b, v->installed))
			snprintf(p->sub, sizeof p->sub, T(S_UPDATE_UPDATED_FROM), a);
		else if (sscanf(v->result, "rolled-back %31s", a) == 1 && strcmp(a, v->installed))
			snprintf(p->sub, sizeof p->sub, T(S_UPDATE_WENT_BACK_FROM), a);
		else if (v->checked[0])
			snprintf(p->sub, sizeof p->sub, T(S_UPDATE_UP_TO_DATE_CHECKED), v->checked);
		else
			snprintf(p->sub, sizeof p->sub, "%s", T(S_UPDATE_UP_TO_DATE));
		if (v->wifi) key(p, "A", T(S_UPDATE_LOOK_AGAIN), SUA_CHECK);
		break;
	}
	case SU_AVAILABLE:
	case SU_BATTERY:
		size_line(v, p->sub, sizeof p->sub);
		p->notes = v->notes;
		download_key(p, v, T(S_UPDATE_DOWNLOAD));
		break;
	case SU_COPYING:
	case SU_DOWNLOADING:
	case SU_VERIFYING:
	case SU_INSTALLING:
		size_line(v, p->sub, sizeof p->sub);
		p->notes = v->notes;
		p->bar = true;
		if (s == SU_COPYING) {
			snprintf(p->bar_left, sizeof p->bar_left, "%s", T(S_UPDATE_PREPARING));
			snprintf(p->bar_right, sizeof p->bar_right, T(S_UPDATE_FILES_OF), v->st.files_done, v->st.files_total);
			p->frac = frac((unsigned long long)v->st.files_done, (unsigned long long)v->st.files_total);
		} else if (s == SU_DOWNLOADING) {
			snprintf(p->bar_left, sizeof p->bar_left, "%s", T(S_UPDATE_DOWNLOADING));
			of_size(v->st.done, v->st.total, p->bar_right, sizeof p->bar_right);
			p->frac = frac(v->st.done, v->st.total);
		} else if (s == SU_INSTALLING) {
			/* The root written into the other slot, then read back */
			snprintf(p->bar_left, sizeof p->bar_left, "%s", T(S_UPDATE_INSTALLING));
			snprintf(p->bar_right, sizeof p->bar_right, "%.0f%%", 100.0 * frac(v->st.done, v->st.total));
			p->frac = frac(v->st.done, v->st.total);
		} else {
			snprintf(p->bar_left, sizeof p->bar_left, "%s", T(S_UPDATE_CHECKING_FILES));
			snprintf(p->bar_right, sizeof p->bar_right, T(S_UPDATE_COUNT_OF), v->st.files_done, v->st.files_total);
			p->frac = frac(v->st.done, v->st.total);
		}
		key(p, "B", T(S_UPDATE_BACK_GOES_ON), SUA_BACK);
		return;
	case SU_STOPPED:
		size_line(v, p->sub, sizeof p->sub);
		p->notes = v->notes;
		if (v->st.total) {
			p->bar = true;
			snprintf(p->bar_left, sizeof p->bar_left, "%s", T(S_UPDATE_STOPPED));
			of_size(v->st.done, v->st.total, p->bar_right, sizeof p->bar_right);
			p->frac = frac(v->st.done, v->st.total);
		}
		download_key(p, v, T(S_UPDATE_CONTINUE));
		if (p->line[0]) p->bar = false;
		break;
	case SU_READY:
		snprintf(p->sub, sizeof p->sub, "%s", v->st.system ? T(S_UPDATE_INSTALLED_CHECKED) : T(S_UPDATE_DOWNLOADED_CHECKED));
		p->notes = v->notes;
		snprintf(p->line, sizeof p->line, "%s", T(S_UPDATE_GOES_IN_NEXT_START));
		key(p, "A", T(S_UPDATE_RESTART_NOW), SUA_RESTART);
		key(p, "B", T(S_UPDATE_LATER), SUA_BACK);
		return;
	case SU_ERROR:
		if (v->st.version[0]) {
			size_line(v, p->sub, sizeof p->sub);
			snprintf(p->warn, sizeof p->warn, "%s", T(S_UPDATE_DOWNLOAD_FAILED));
			if (v->wifi) key(p, "A", T(S_UPDATE_TRY_AGAIN), SUA_DOWNLOAD);
		} else {
			snprintf(p->sub, sizeof p->sub, "%s", T(S_UPDATE_COULD_NOT_LOOK));
			snprintf(p->warn, sizeof p->warn, "%s", T(S_UPDATE_NO_SERVER));
			if (v->wifi) key(p, "A", T(S_UPDATE_TRY_AGAIN), SUA_CHECK);
		}
		break;
	case SU_SPACE:
		size_line(v, p->sub, sizeof p->sub);
		snprintf(p->warn, sizeof p->warn, "%s", T(S_UPDATE_CARD_FULL));
		break;
	case SU_NEEDS_SYSTEM:
		snprintf(p->sub, sizeof p->sub, T(S_UPDATE_NEEDS_SYSTEM), v->st.version);
		snprintf(p->line, sizeof p->line, "%s", T(S_UPDATE_USE_INSTALLER));
		break;
	case SU_TOO_BIG:
		/* A card keeps the slot size it was laid out with
		 * (docs/updates.md): only a fresh install makes them bigger */
		snprintf(p->sub, sizeof p->sub, "%s", T(S_UPDATE_NEEDS_FRESH));
		snprintf(p->warn, sizeof p->warn, "%s", T(S_UPDATE_TOO_BIG));
		snprintf(p->line, sizeof p->line, "%s", T(S_UPDATE_USE_INSTALLER));
		break;
	case SU_ROLLBACK:
		snprintf(p->sub, sizeof p->sub, T(S_UPDATE_GOING_BACK_TO), v->st.version);
		snprintf(p->line, sizeof p->line, "%s", T(S_UPDATE_GOES_BACK_NEXT_START));
		key(p, "A", T(S_UPDATE_RESTART_NOW), SUA_RESTART);
		key(p, "B", T(S_UPDATE_LATER), SUA_BACK);
		return;
	case SU_UNSUPPORTED:
		break;
	}
	if (went_back) {
		snprintf(p->sub, sizeof p->sub, T(S_UPDATE_BACK_ON), p->from);
		snprintf(p->warn, sizeof p->warn, T(S_UPDATE_WENT_BACK_ITSELF),
		         v->result + 7, p->from);
		p->to[0] = 0;
		if (p->nkeys && p->keys[0].act == SUA_DOWNLOAD) snprintf(p->keys[0].label, sizeof p->keys[0].label, "%s", T(S_UPDATE_TRY_AGAIN));
	}
	key(p, "B", T(S_UPDATE_BACK), SUA_BACK);
	rollback_key(p, v);
	/* A problem takes What's New's place */
	if (p->warn[0]) p->notes = false;
}

const char *su_menu_value(const su_view *v, char *buf, size_t n)
{
	switch (v->running ? (v->st.state == SU_CHECKING ? SU_IDLE : v->st.state) : effective(v)) {
	case SU_READY:       snprintf(buf, n, "%s", T(S_UPDATE_VALUE_RESTART_UPDATE)); break;
	case SU_ROLLBACK:    snprintf(buf, n, "%s", T(S_UPDATE_VALUE_RESTART_BACK)); break;
	case SU_COPYING:
	case SU_DOWNLOADING:
	case SU_VERIFYING:
	case SU_INSTALLING:
		if (v->running) snprintf(buf, n, "%s", T(S_UPDATE_VALUE_DOWNLOADING));
		else snprintf(buf, n, T(S_UPDATE_VALUE_AVAILABLE), v->st.version);
		break;
	case SU_AVAILABLE:
	case SU_BATTERY:
	case SU_STOPPED:     snprintf(buf, n, T(S_UPDATE_VALUE_AVAILABLE), v->st.version); break;
	default:             snprintf(buf, n, "%s", v->installed[0] ? v->installed : ""); break;
	}
	return buf;
}
