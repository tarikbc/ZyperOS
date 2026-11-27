/* SPDX-License-Identifier: GPL-3.0-only */
/* See gadget.h. */
#include "gadget.h"

#include "cablefresh.h"
#include "hwput.h"
#include "linksteps.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static gadget_cfg g;
static char       g_udc_dir[320], g_udc_name[128];

/* The job on its way, and what was asked since */
enum { J_NONE, J_BIND, J_UNBIND, J_REBIND, J_FRESH };
static int      g_job = J_NONE;
static int      g_want = -1;           /* -1 nothing, 0 off, 1 on, 2 off and on */
static bool     g_fresh;
static int      g_known = -1;          /* bound, as the last job or read left it */
static unsigned g_bound_at, g_fail_at;
static bool     g_failed;
static bool     g_before;

void gadget_test_before(bool on) { g_before = on; }

void gadget_init(const gadget_cfg *c)
{
	g = *c;
	g_udc_dir[0] = g_udc_name[0] = '\0';
	g_job = J_NONE;
	g_want = -1;
	g_fresh = g_failed = false;
	g_known = -1;
	g_bound_at = g_fail_at = 0;
}

static unsigned now(void) { return g.now_ms ? g.now_ms() : 0; }

static bool read_small(const char *path, char *buf, size_t n)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t got;

	buf[0] = '\0';
	if (fd < 0) return false;
	got = read(fd, buf, n - 1);
	close(fd);
	buf[got > 0 ? got : 0] = '\0';
	return true;
}

/* The gadget's UDC file: bound when it names a controller. False with no
 * gadget (plastron's S40usbgadget has not made it). Only read with no job
 * on its way: a read waits on the lock a hung write holds. */
static bool read_bound(bool *bound)
{
	char cur[64];

	if (!g.gadget_udc || !read_small(g.gadget_udc, cur, sizeof cur)) return false;
	*bound = cur[0] != '\0' && cur[0] != '\n';
	g_known = *bound;
	return true;
}

const char *gadget_udc_dir(void)
{
	DIR *d;
	struct dirent *e;

	if (g_udc_dir[0]) return g_udc_dir;
	if (g.udc) {
		const char *base = strrchr(g.udc, '/');

		snprintf(g_udc_dir, sizeof g_udc_dir, "%s", g.udc);
		snprintf(g_udc_name, sizeof g_udc_name, "%s", base ? base + 1 : g.udc);
		return g_udc_dir;
	}
	if (!g.udc_class || !(d = opendir(g.udc_class))) return NULL;
	while ((e = readdir(d)))
		if (e->d_name[0] != '.') {
			snprintf(g_udc_name, sizeof g_udc_name, "%s", e->d_name);
			snprintf(g_udc_dir, sizeof g_udc_dir, "%s/%s", g.udc_class, e->d_name);
			break;
		}
	closedir(d);
	return g_udc_dir[0] ? g_udc_dir : NULL;
}

int gadget_configured(void)
{
	char path[360], state[64];
	const char *dir = gadget_udc_dir();

	if (!g.state_tells || !dir) return -1;
	snprintf(path, sizeof path, "%s/state", dir);
	if (!read_small(path, state, sizeof state)) return -1;
	return link_udc_configured(state);
}

static void finish(int kind, int r);

/* A bind, an unbind or both, in hwput's child, not waited for */
static void start_job(int kind, bool unbind, bool bind)
{
	char say_bind[200];
	hwput_step steps[2];
	int n = 0, r;

	if (bind && !gadget_udc_dir()) {
		fprintf(stderr, "usb: no controller for the link\n");
		return;
	}
	if (unbind) steps[n++] = (hwput_step){ "usb: the gadget's UDC, unbind", g.gadget_udc, "\n", 0 };
	if (bind) {
		snprintf(say_bind, sizeof say_bind, "usb: the gadget's UDC, bind %s", g_udc_name);
		steps[n++] = (hwput_step){ say_bind, g.gadget_udc, g_udc_name, 0 };
	}
	r = hwput_run(&(hwput_job){ bind ? "the gadget's bind" : "the gadget's unbind", NULL, NULL,
	                            steps, n }, g_before);
	if (r == HWPUT_STARTED) g_job = kind;
	else if (g_before) finish(kind, r);
}

/* A job's end, said */
static void finish(int kind, int r)
{
	bool b = false;

	switch (kind) {
	case J_BIND:
	case J_REBIND:
		if (r == HWPUT_DONE) {
			g_known = 1;
			g_bound_at = now() ? now() : 1;
			g_failed = false;
			fprintf(stderr, "usb: the link to the computer is on%s (%s)\n",
			        kind == J_REBIND ? ", fresh" : "", g_udc_name);
			if (g.after_bind) g.after_bind();
		} else {
			g_failed = true;
			g_fail_at = now();
			read_bound(&b);
			fprintf(stderr, "usb: the link to the computer did not come on: again in %u s at most\n",
			        GADGET_RETRY_MS / 1000);
		}
		break;
	case J_UNBIND:
		if (r == HWPUT_DONE) {
			g_known = 0;
			fprintf(stderr, "usb: the link to the computer is off: charging only\n");
		} else {
			read_bound(&b);
			fprintf(stderr, "usb: the link to the computer did not go off\n");
		}
		break;
	default:
		break;                       /* the fresh link's child said its own */
	}
}

void gadget_pump(void)
{
	bool b;

	if (g_job != J_NONE) {
		int r = hwput_take();

		if (r < 0) {
			if (hwput_busy()) return;        /* on its way still */
			r = HWPUT_FAILED;
		}
		finish(g_job, r);
		g_job = J_NONE;
	} else if (hwput_busy()) {
		return;
	}
	if (g_want >= 0) {
		if (!read_bound(&b)) {
			g_want = -1;                     /* no gadget to bind */
			return;
		}
		if (g_want == 2) {
			g_want = -1;
			start_job(J_REBIND, b, true);
			return;
		}
		if (g_want == 1 && !b) {
			if (g_failed && now() - g_fail_at < GADGET_RETRY_MS) return;
			g_want = -1;
			start_job(J_BIND, false, true);
			return;
		}
		if (g_want == 0 && b) {
			g_want = -1;
			start_job(J_UNBIND, true, false);
			return;
		}
		g_want = -1;                         /* as asked already */
	}
	if (g_fresh) {
		const char *dir = gadget_udc_dir();

		g_fresh = false;
		if (!dir) return;
		if (g_before) {
			char path[360];
			hwput_step steps[2];

			snprintf(path, sizeof path, "%s/soft_connect", dir);
			steps[0] = (hwput_step){ "usb: soft_connect disconnect", path, "disconnect", 300 };
			steps[1] = (hwput_step){ "usb: soft_connect connect", path, "connect", 0 };
			hwput_run(&(hwput_job){ "soft_connect", NULL, NULL, steps, 2 }, true);
			return;
		}
		cable_fresh(&(cable_fresh_cfg){ dir, g.ifname, g.peer, g.state_tells,
		                                g_bound_at && now() - g_bound_at < LINK_FRESH_MS, g.net });
		if (hwput_busy()) g_job = J_FRESH;
	}
}

bool gadget_link(int op)
{
	bool b;

	gadget_pump();
	if (op < 0) {
		if (g_want == 0) return false;
		if (g_want >= 1) return true;
		if (g_job == J_BIND || g_job == J_REBIND) return true;
		if (g_job == J_UNBIND) return false;
		if (hwput_busy()) return g_known == 1;
		return read_bound(&b) && b;
	}
	if (op == 1) {
		if (g_want < 0 && g_job == J_NONE && !hwput_busy()) {
			if (!read_bound(&b)) return false;       /* no gadget */
			if (b) return true;
		}
		if (g_want != 2) g_want = 1;
	} else if (op == 0) {
		g_want = 0;
	} else {
		g_want = 2;
	}
	gadget_pump();
	return op != 0;
}

void gadget_fresh(void)
{
	g_fresh = true;
	gadget_pump();
}

bool gadget_idle(void)
{
	return g_want < 0 && !g_fresh && g_job == J_NONE && !hwput_busy();
}
