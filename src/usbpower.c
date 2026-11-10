/* SPDX-License-Identifier: GPL-3.0-only */
/* See usbpower.h. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "usbpower.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

bool usbpower_parse(const char *text, usb_power *o)
{
	char role[16] = "";
	int ask = 0, powered = 0;

	memset(o, 0, sizeof *o);
	if (!text || sscanf(text, "plug=%u ask=%d pin=%d vbus=%d powered=%d role=%15s",
	                    &o->plug, &ask, &o->pin, &o->vbus, &powered, role) != 6)
		return false;
	if (strcmp(role, "host") && strcmp(role, "device"))
		return false;
	o->ask = ask == 1;
	o->powered = powered == 1;
	o->host = !strcmp(role, "host");
	o->known = true;
	return true;
}

bool usbpower_read(const char *dir, usb_power *o)
{
	char path[256], text[128];
	ssize_t got;
	int fd;

	memset(o, 0, sizeof *o);
	snprintf(path, sizeof path, "%s/state", dir);
	if ((fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC)) < 0) return false;
	got = read(fd, text, sizeof text - 1);
	close(fd);
	if (got <= 0) return false;
	text[got] = '\0';
	return usbpower_parse(text, o);
}

bool usbpower_request(const char *dir, unsigned plug)
{
	char tmp[256], path[256], text[16];
	int fd, n;
	bool ok;

	snprintf(tmp, sizeof tmp, "%s/host.new", dir);
	snprintf(path, sizeof path, "%s/host", dir);
	n = snprintf(text, sizeof text, "%u\n", plug);
	if ((fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600)) < 0)
		return false;
	ok = write(fd, text, (size_t)n) == n;
	close(fd);
	if (!ok || rename(tmp, path) < 0) {
		unlink(tmp);
		return false;
	}
	return true;
}

bool usbpower_waits(const usb_power *p)
{
	return p->known && p->pin == 1 && p->vbus == 0 && !p->host;
}

unsigned usbpower_follow(usbpower_follow_state *f, const usb_power *p, unsigned now)
{
	if (!p->known || !p->ask || !p->plug) {
		f->since = 0;
		return 0;
	}
	if (p->plug != f->plug) {
		f->plug = p->plug;
		f->since = 0;
	}
	if (!f->since) f->since = now ? now : 1;
	if (f->asked == p->plug || now - f->since < USBPOWER_ASK_MS) return 0;
	f->asked = p->plug;
	return p->plug;
}
