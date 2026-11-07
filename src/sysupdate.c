/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* What MENU > System Update says. See src/sysupdate.h, and the update lab
 * (docs/design/mock/update-lab.html) for the page this lays out. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sysupdate.h"

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
	else                           snprintf(buf, n, "%llu bytes", b);
}

/* "6.1 of 14 MB": the unit once, the larger one's */
static void of_size(unsigned long long done, unsigned long long total, char *buf, size_t n)
{
	char t[24];
	su_size(total, t, sizeof t);
	if (total >= 1000ull * 1000) {
		double d = (double)done / 1e6;
		if (d >= 10) snprintf(buf, n, "%.0f of %s", d, t);
		else snprintf(buf, n, "%.1f of %s", d, t);
	} else {
		snprintf(buf, n, "%llu of %s", (done + 500) / 1000, t);
	}
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
		snprintf(buf, n, "System and ZyperOS \xc2\xb7 %s", d);
	else if (v->st.size) {
		su_size(v->st.size, t, sizeof t);
		snprintf(buf, n, "%s to download \xc2\xb7 %s in all", d, t);
	} else {
		snprintf(buf, n, "%s to download", d);
	}
}

/* Download (or Continue): refused, with the reason where the bar goes, when
 * it cannot work */
static void download_key(su_page *p, const su_view *v, const char *label)
{
	if (!v->wifi) {
		snprintf(p->line, sizeof p->line, "Connect to Wi-Fi to download.");
	} else if (v->battery < SU_MIN_BATTERY && !v->charging) {
		snprintf(p->warn, sizeof p->warn,
		         "The battery is at %d%%. Plug in the charger to download the update.", v->battery);
	} else {
		key(p, "A", label, SUA_DOWNLOAD);
	}
}

static void rollback_key(su_page *p, const su_view *v)
{
	char l[32];
	if (!v->prev[0]) return;
	snprintf(l, sizeof l, "Roll Back to %s", v->prev);
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
		snprintf(p->sub, sizeof p->sub, "System Update is not on this system.");
		snprintf(p->line, sizeof p->line, "Use the ZyperOS Installer.");
		key(p, "B", "Back", SUA_BACK);
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
			snprintf(p->sub, sizeof p->sub, "Not looked for updates yet");
			key(p, "A", "Look for Updates", SUA_CHECK);
		} else {
			snprintf(p->sub, sizeof p->sub, "Connect to Wi-Fi to look for updates.");
		}
		break;
	case SU_CHECKING:
		snprintf(p->sub, sizeof p->sub, "Looking for updates\xe2\x80\xa6");
		break;
	case SU_UPTODATE: {
		/* Just switched (update/apply.sh's result): say from what */
		char a[32] = "", b[32] = "";
		if (sscanf(v->result, "updated %31s %31s", a, b) == 2 && !strcmp(b, v->installed))
			snprintf(p->sub, sizeof p->sub, "Updated from %s", a);
		else if (sscanf(v->result, "rolled-back %31s", a) == 1 && strcmp(a, v->installed))
			snprintf(p->sub, sizeof p->sub, "Went back from %s", a);
		else if (v->checked[0])
			snprintf(p->sub, sizeof p->sub, "Up to date \xc2\xb7 checked %s", v->checked);
		else
			snprintf(p->sub, sizeof p->sub, "Up to date");
		if (v->wifi) key(p, "A", "Look Again", SUA_CHECK);
		break;
	}
	case SU_AVAILABLE:
	case SU_BATTERY:
		size_line(v, p->sub, sizeof p->sub);
		p->notes = v->notes;
		download_key(p, v, "Download");
		break;
	case SU_COPYING:
	case SU_DOWNLOADING:
	case SU_VERIFYING:
	case SU_INSTALLING:
		size_line(v, p->sub, sizeof p->sub);
		p->notes = v->notes;
		p->bar = true;
		if (s == SU_COPYING) {
			snprintf(p->bar_left, sizeof p->bar_left, "Preparing");
			snprintf(p->bar_right, sizeof p->bar_right, "%d of %d files", v->st.files_done, v->st.files_total);
			p->frac = frac((unsigned long long)v->st.files_done, (unsigned long long)v->st.files_total);
		} else if (s == SU_DOWNLOADING) {
			snprintf(p->bar_left, sizeof p->bar_left, "Downloading");
			of_size(v->st.done, v->st.total, p->bar_right, sizeof p->bar_right);
			p->frac = frac(v->st.done, v->st.total);
		} else if (s == SU_INSTALLING) {
			/* The root written into the other slot, then read back */
			snprintf(p->bar_left, sizeof p->bar_left, "Installing");
			snprintf(p->bar_right, sizeof p->bar_right, "%.0f%%", 100.0 * frac(v->st.done, v->st.total));
			p->frac = frac(v->st.done, v->st.total);
		} else {
			snprintf(p->bar_left, sizeof p->bar_left, "Checking files");
			snprintf(p->bar_right, sizeof p->bar_right, "%d of %d", v->st.files_done, v->st.files_total);
			p->frac = frac(v->st.done, v->st.total);
		}
		key(p, "B", "Back, it goes on", SUA_BACK);
		return;
	case SU_STOPPED:
		size_line(v, p->sub, sizeof p->sub);
		p->notes = v->notes;
		if (v->st.total) {
			p->bar = true;
			snprintf(p->bar_left, sizeof p->bar_left, "Stopped");
			of_size(v->st.done, v->st.total, p->bar_right, sizeof p->bar_right);
			p->frac = frac(v->st.done, v->st.total);
		}
		download_key(p, v, "Continue");
		if (p->line[0]) p->bar = false;
		break;
	case SU_READY:
		snprintf(p->sub, sizeof p->sub, v->st.system ? "Installed and checked" : "Downloaded and checked");
		p->notes = v->notes;
		snprintf(p->line, sizeof p->line, "Or it goes in at the next start.");
		key(p, "A", "Restart Now", SUA_RESTART);
		key(p, "B", "Later", SUA_BACK);
		return;
	case SU_ERROR:
		if (v->st.version[0]) {
			size_line(v, p->sub, sizeof p->sub);
			snprintf(p->warn, sizeof p->warn,
			         "The download did not finish. Check the Wi-Fi and try again.");
			if (v->wifi) key(p, "A", "Try Again", SUA_DOWNLOAD);
		} else {
			snprintf(p->sub, sizeof p->sub, "Could not look for updates");
			snprintf(p->warn, sizeof p->warn,
			         "Could not reach the update server. Check the Wi-Fi and try again.");
			if (v->wifi) key(p, "A", "Try Again", SUA_CHECK);
		}
		break;
	case SU_SPACE:
		size_line(v, p->sub, sizeof p->sub);
		snprintf(p->warn, sizeof p->warn,
		         "The card is too full for the update. Make room on it, then try again.");
		break;
	case SU_NEEDS_SYSTEM:
		snprintf(p->sub, sizeof p->sub, "%s also changes the system", v->st.version);
		snprintf(p->line, sizeof p->line, "Use the ZyperOS Installer.");
		break;
	case SU_TOO_BIG:
		/* A card keeps the slot size it was laid out with
		 * (docs/updates.md): only a fresh install makes them bigger */
		snprintf(p->sub, sizeof p->sub, "Needs a fresh install");
		snprintf(p->warn, sizeof p->warn,
		         "Too big for this card's system slots. A fresh install erases the card.");
		snprintf(p->line, sizeof p->line, "Use the ZyperOS Installer.");
		break;
	case SU_ROLLBACK:
		snprintf(p->sub, sizeof p->sub, "Going back to %s", v->st.version);
		snprintf(p->line, sizeof p->line, "Or it goes back at the next start.");
		key(p, "A", "Restart Now", SUA_RESTART);
		key(p, "B", "Later", SUA_BACK);
		return;
	case SU_UNSUPPORTED:
		break;
	}
	if (went_back) {
		snprintf(p->sub, sizeof p->sub, "Back on %s", p->from);
		snprintf(p->warn, sizeof p->warn,
		         "%s did not start twice, so ZyperOS went back to %s by itself.",
		         v->result + 7, p->from);
		p->to[0] = 0;
		if (p->nkeys && p->keys[0].act == SUA_DOWNLOAD) snprintf(p->keys[0].label, sizeof p->keys[0].label, "Try Again");
	}
	key(p, "B", "Back", SUA_BACK);
	rollback_key(p, v);
	/* A problem takes What's New's place */
	if (p->warn[0]) p->notes = false;
}

const char *su_menu_value(const su_view *v, char *buf, size_t n)
{
	switch (v->running ? (v->st.state == SU_CHECKING ? SU_IDLE : v->st.state) : effective(v)) {
	case SU_READY:       snprintf(buf, n, "restart to update"); break;
	case SU_ROLLBACK:    snprintf(buf, n, "restart to go back"); break;
	case SU_COPYING:
	case SU_DOWNLOADING:
	case SU_VERIFYING:
	case SU_INSTALLING:  snprintf(buf, n, v->running ? "downloading" : "%s available", v->st.version); break;
	case SU_AVAILABLE:
	case SU_BATTERY:
	case SU_STOPPED:     snprintf(buf, n, "%s available", v->st.version); break;
	default:             snprintf(buf, n, "%s", v->installed[0] ? v->installed : ""); break;
	}
	return buf;
}
