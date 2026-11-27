/* SPDX-License-Identifier: GPL-3.0-only */
/* Desk mode's stand-in USB cable (deskcable.h): the Pixel 2's link code
 * (gadget.h, cablefresh.h, hwput.h) over a folder of files in place of
 * sysfs, so File Transfer's opening runs on a computer as on the handheld,
 * a slow controller and all. tools/xferopen-check.py drives it. */
#include "deskcable.h"

#include "../cablefresh.h"
#include "../gadget.h"
#include "../hwput.h"
#include "../linksteps.h"
#include "../platform.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *g_root;
static char        g_answers[600];

static const char *root(void)
{
	static bool looked;

	if (!looked) {
		const char *r = getenv("ZYPEROS_DESK_CABLE");

		looked = true;
		g_root = r && *r ? r : NULL;
	}
	return g_root;
}

bool deskcable_on(void) { return root() != NULL; }

static bool file_is(const char *name, const char *word)
{
	char path[600], buf[64] = "";
	int fd;
	ssize_t got;

	snprintf(path, sizeof path, "%s/%s", root(), name);
	if ((fd = open(path, O_RDONLY | O_CLOEXEC)) < 0) return false;
	got = read(fd, buf, sizeof buf - 1);
	close(fd);
	buf[got > 0 ? got : 0] = '\0';
	return !strncmp(buf, word, strlen(word));
}

/* The computer's answer over the cable, read in the fresh link's child:
 * open and read only */
static bool answers(void)
{
	char c = '0';
	int fd = open(g_answers, O_RDONLY | O_CLOEXEC);

	if (fd >= 0) {
		if (read(fd, &c, 1) != 1) c = '0';
		close(fd);
	}
	return c == '1';
}

static void setup(void)
{
	static bool done;
	static char udc[600], gadget[600], net[600];
	const char *d = getenv("ZYPEROS_DESK_CABLE_DELAY_MS");

	if (done) return;
	done = true;
	snprintf(udc, sizeof udc, "%s/udc/fake.udc", root());
	snprintf(gadget, sizeof gadget, "%s/gadget/UDC", root());
	snprintf(net, sizeof net, "%s/net", root());
	snprintf(g_answers, sizeof g_answers, "%s/answers", root());
	gadget_init(&(gadget_cfg){ udc, NULL, gadget, net, "usb0", "10.42.0.2", true, NULL, plat_now_ms });
	cable_fresh_test_probe(answers);
	if (d) hwput_test_delay((unsigned)atoi(d));
	/* 1.1.0's, for the check's "before": every write waited for */
	if (getenv("ZYPEROS_DESK_CABLE_BEFORE")) {
		gadget_test_before(true);
		hwput_set_timeout(60000);
	}
}

bool deskcable_link(char *addr, size_t n)
{
	if (addr && n) snprintf(addr, n, "10.42.0.1");
	return true;
}

usb_peer deskcable_peer(void)
{
	setup();
	gadget_pump();
	return file_is("peer", "computer") ? USB_PEER_COMPUTER
	     : file_is("peer", "charger") ? USB_PEER_CHARGER : USB_PEER_NONE;
}

bool deskcable_usb_link(int op)
{
	setup();
	return gadget_link(op);
}

int deskcable_configured(void)
{
	setup();
	gadget_pump();
	return gadget_configured();
}

bool deskcable_sense(link_sense *o)
{
	memset(o, 0, sizeof *o);
	o->computer = deskcable_peer() == USB_PEER_COMPUTER;
	o->configured = o->computer && gadget_configured() == 1;
	o->carrier = o->configured && file_is("net/usb0/carrier", "1");
	o->address = o->carrier && file_is("address", "1");
	return true;
}

void deskcable_reconnect(void)
{
	setup();
	gadget_fresh();
}
