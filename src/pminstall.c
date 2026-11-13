/* SPDX-License-Identifier: GPL-3.0-only */
/* What the Install PortMaster page says. See src/pminstall.h. */
#include <ctype.h>
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
		} else if (!strcmp(line, "kind")) {
			out->reinstall = !strcmp(val, "reinstall");
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

/* "Version 2026.06.23-0015 · installed": the PortMaster on the card, which a
 * reinstall's page names until the fresh one is in */
static void have_line(const pm_view *v, char *buf, size_t n)
{
	if (v->have[0]) snprintf(buf, n, T(S_PORTMASTER_INSTALLED), v->have);
	else snprintf(buf, n, "%s", T(S_PORTMASTER_FROM));
}

void pm_page_build(const pm_view *v, su_page *p, char notes[PM_NOTES][120], int *nnotes)
{
	pm_state s = v->st.state;
	bool rep = v->repair;

	memset(p, 0, sizeof *p);
	*nnotes = 0;
	snprintf(p->from, sizeof p->from, "%s", "PortMaster");
	if (rep) {
		/* A reinstall: the offer until A, or until one runs. The status
		 * file can be the first install's, from long ago, and says nothing
		 * of this page until its own run writes to it */
		if (!v->running && !v->asked) s = PM_IDLE;
		else if (v->running && s == PM_IDLE) s = PM_CHECKING;
	} else {
		/* The installer's word stands while it runs; once it is gone, the
		 * card does. A run cut short (killed, the handheld turned off) said
		 * nothing more, and is a stop. PortMaster on the card is done
		 * whatever the file says: put there by hand, or by a run before
		 * this start. */
		if (v->running && s == PM_IDLE) s = PM_CHECKING;
		if (!v->running) {
			if (v->installed) s = PM_DONE;
			else if (s == PM_DONE) s = PM_IDLE;
		}
	}
	if (!v->python && !v->running && (rep || !v->installed)) {
		/* A system from before PortMaster had Python to run on: nothing to
		 * offer until the system is updated (a reinstall names the
		 * PortMaster that is there, copied onto an old system's card) */
		if (rep) have_line(v, p->sub, sizeof p->sub);
		else snprintf(p->sub, sizeof p->sub, "%s", T(S_PORTMASTER_NOT_INSTALLED));
		snprintf(p->warn, sizeof p->warn, "%s", T(S_PORTMASTER_NEEDS_SYSTEM));
		key(p, "B", T(S_UPDATE_BACK), SUA_BACK);
		return;
	}

	/* What PortMaster is, under the line, from the offer until it is in:
	 * the page is never a bar alone. A reinstall's: what it does, and what
	 * of the player's it keeps */
	if (s != PM_DONE && s != PM_ERROR) {
		p->notes = true;
		if (rep) {
			note(notes, nnotes, T(S_PORTMASTER_REINSTALL_KEEPS));
			note(notes, nnotes, T(S_PORTMASTER_REINSTALL_RESETS));
		} else {
			note(notes, nnotes, T(S_PORTMASTER_POINT_STORE));
			note(notes, nnotes, T(S_PORTMASTER_POINT_FILES));
		}
	}
	switch (s) {
	case PM_IDLE:
		if (rep && v->crashed) {
			/* PortMaster closed with an error: said where the points go,
			 * and the line of its log that says how where the bar goes,
			 * in PortMaster's own words (where the reinstall downloads
			 * from goes without saying here). With no network, that it
			 * needs one says more */
			p->notes = false;
			*nnotes = 0;
			snprintf(p->warn, sizeof p->warn, "%s", T(S_PORTMASTER_FAILED));
			have_line(v, p->sub, sizeof p->sub);
			if (v->failed[0]) snprintf(p->line, sizeof p->line, "%s", v->failed);
		} else if (rep) {
			have_line(v, p->sub, sizeof p->sub);
		} else {
			snprintf(p->sub, sizeof p->sub, "%s", T(S_PORTMASTER_FROM));
		}
		if (!v->network) p->line[0] = 0;
		install_key(p, v, T(rep ? S_PORTMASTER_REINSTALL : S_PORTMASTER_INSTALL));
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
			/* A reinstall cut off left the PortMaster that was there */
			if (rep) have_line(v, p->sub, sizeof p->sub);
			else snprintf(p->sub, sizeof p->sub, "%s", T(S_PORTMASTER_NOT_INSTALLED));
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
		note(notes, nnotes, T(rep ? S_PORTMASTER_REINSTALLED : S_PORTMASTER_DONE_SHELF));
		note(notes, nnotes, T(S_PORTMASTER_DONE_OPEN));
		key(p, "A", T(S_PORTMASTER_OPEN), SUA_OPEN);
		break;
	case PM_ERROR:
		/* A reinstall that failed left the PortMaster that was there */
		if (rep) have_line(v, p->sub, sizeof p->sub);
		else snprintf(p->sub, sizeof p->sub, "%s", T(S_PORTMASTER_NOT_INSTALLED));
		error_words(&v->st, p->warn, sizeof p->warn);
		install_key(p, v, T(S_UPDATE_TRY_AGAIN));
		break;
	}
	key(p, "B", T(S_UPDATE_BACK), SUA_BACK);
}

/* ---- pm_failure ----------------------------------------------------------- */

/* Lines of the log that say nothing of how PortMaster ended: blank, the
 * launcher script's own tidying after its program (systemctl, which ZyperOS
 * has none of), the display's and the sound's chatter on the way out */
static bool pm_noise(const char *l)
{
	static const char *const NOISE[] = {
		"systemctl", "Could not restore CRTC", "virtio_gpu", "[ALSOFT]",
		"No sudo present", "Terminated",
	};
	size_t i;

	while (*l == ' ' || *l == '\t') l++;
	if (!*l) return true;
	for (i = 0; i < sizeof NOISE / sizeof *NOISE; i++)
		if (strstr(l, NOISE[i])) return true;
	return false;
}

/* An exception's own line, at the start of a line: "ModuleNotFoundError: No
 * module named 'x'", "sdl2.ext.common.SDLError: ...", "KeyError: 'name'". A
 * dotted name of letters, digits and _, then ": " or the end; the name ends
 * in Error, Exception, Exit or Interrupt, as Python's and pysdl2's all do. A
 * loguru line ("2026-11-12 18:05:59.771 | DEBUG | ...") starts with a digit
 * and is never one. */
static bool pm_exception_line(const char *l)
{
	static const char *const ENDS[] = { "Error", "Exception", "Exit", "Interrupt" };
	const char *e = l;
	size_t i, n;

	if (!isalpha((unsigned char)*l) && *l != '_') return false;
	while (isalnum((unsigned char)*e) || *e == '_' || *e == '.') e++;
	if (*e && !(e[0] == ':' && (e[1] == ' ' || !e[1]))) return false;
	n = (size_t)(e - l);
	for (i = 0; i < sizeof ENDS / sizeof *ENDS; i++) {
		size_t k = strlen(ENDS[i]);
		if (n >= k && !strncmp(e - k, ENDS[i], k)) return true;
	}
	return false;
}

static void pm_why(const char *l, size_t len, char *why, size_t n)
{
	size_t k;

	while (len && (*l == ' ' || *l == '\t')) { l++; len--; }
	while (len && (l[len - 1] == ' ' || l[len - 1] == '\r')) len--;
	if (n < 5) { if (n) why[0] = 0; return; }
	if (len < n) {
		memcpy(why, l, len);
		why[len] = 0;
		return;
	}
	/* Too long: cut on a character, not in the middle of one, with room
	 * for "…" after it */
	k = n - 4;
	while (k && ((unsigned char)l[k] & 0xC0) == 0x80) k--;
	memcpy(why, l, k);
	memcpy(why + k, "\xE2\x80\xA6", 4);
}

/* How many lines that say something may follow an error in the log for it
 * to be the end of the program: the launcher script's last words, bash's
 * report of the program's death */
#define PM_TAIL_LINES 6

bool pm_failure(const char *log, char *why, size_t n)
{
	static const char *const SIGNALS[] = {
		"Segmentation fault", "Bus error", "Illegal instruction", "Aborted",
		"Floating point exception", "Killed",
	};
	const char *p = log, *exc = NULL, *fatal = NULL, *sig = NULL, *video = NULL;
	size_t exc_len = 0, fatal_len = 0, sig_len = 0, video_len = 0;
	int after_exc = 0;
	bool traceback = false;

	if (n) why[0] = 0;
	if (!log) return false;
	while (*p) {
		const char *e = strchr(p, '\n');
		size_t len = e ? (size_t)(e - p) : strlen(p);
		char line[512];
		size_t k = len < sizeof line - 1 ? len : sizeof line - 1, i;

		memcpy(line, p, k);
		line[k] = 0;
		if (strstr(line, "Traceback (most recent call last)") ||
		    strstr(line, "An error has been caught in function")) {
			traceback = true;
		} else if (traceback && pm_exception_line(line)) {
			/* The last exception line of a traceback: the one it ends on */
			exc = p;
			exc_len = len;
			after_exc = 0;
		} else if (exc && !pm_noise(line) && !(line[0] == ' ' || line[0] == '\t')) {
			after_exc++;
			if (after_exc > PM_TAIL_LINES) {
				/* PortMaster went on after it: a caught one */
				exc = NULL;
				traceback = false;
			}
		}
		if (strstr(line, "Fatal Python error")) {
			fatal = strstr(p, "Fatal Python error");
			fatal_len = len - (size_t)(fatal - p);
		}
		if (strstr(line, "pugwash"))
			for (i = 0; i < sizeof SIGNALS / sizeof *SIGNALS; i++)
				if (strstr(line, SIGNALS[i])) {
					sig = SIGNALS[i];
					sig_len = strlen(sig);
				}
		if (strstr(line, "No available video device")) {
			video = strstr(p, "No available video device");
			video_len = strlen("No available video device");
		}
		p = e ? e + 1 : p + len;
	}
	if (sig) pm_why(sig, sig_len, why, n);
	else if (fatal) pm_why(fatal, fatal_len, why, n);
	else if (exc) pm_why(exc, exc_len, why, n);
	else if (video) pm_why(video, video_len, why, n);
	else return false;
	return true;
}

/* ---- pm_json_whole -------------------------------------------------------- */

static bool pm_space(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool pm_json_whole(const char *head, size_t hn, const char *tail, size_t tn)
{
	size_t i = 0;
	char open, close;

	if (hn >= 3 && !memcmp(head, "\xEF\xBB\xBF", 3)) i = 3;
	while (i < hn && pm_space(head[i])) i++;
	if (i >= hn || (head[i] != '{' && head[i] != '[')) return false;
	open = head[i];
	close = open == '{' ? '}' : ']';
	while (tn && pm_space(tail[tn - 1])) tn--;
	return tn && tail[tn - 1] == close;
}

bool pm_json_balanced(const char *buf, size_t n)
{
	size_t i;
	int depth = 0;
	bool str = false, esc = false, seen = false;

	for (i = 0; i < n; i++) {
		char c = buf[i];

		if (!c) return false;
		if (str) {
			if (esc) esc = false;
			else if (c == '\\') esc = true;
			else if (c == '"') str = false;
			continue;
		}
		if (seen && depth == 0) {
			if (!pm_space(c)) return false;
			continue;
		}
		if (c == '"') str = true;
		else if (c == '{' || c == '[') { depth++; seen = true; }
		else if (c == '}' || c == ']') { if (--depth < 0) return false; }
		else if (!seen && !pm_space(c) && !(i < 3 && (unsigned char)c >= 0x80)) return false;
	}
	return seen && depth == 0 && !str;
}

bool pm_json_resets_settings(const char *name)
{
	size_t n = strlen(name), k = strlen(".source.json");

	return !strcmp(name, PM_SETTINGS) || (n > k && !strcmp(name + n - k, ".source.json"));
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
