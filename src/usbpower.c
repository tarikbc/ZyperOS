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
	int ask = 0, computer = 0, automatic = 0, paused = 0, got;

	memset(o, 0, sizeof *o);
	if (!text)
		return false;
	got = sscanf(text, "plug=%u ask=%d pin=%d vbus=%d computer=%d role=%15s auto=%d paused=%d",
	             &o->plug, &ask, &o->pin, &o->vbus, &computer, role, &automatic, &paused);
	if (got != 7 && got != 8)
		return false;
	if (strcmp(role, "host") && strcmp(role, "device"))
		return false;
	o->ask = ask == 1;
	o->computer = computer == 1;
	o->automatic = automatic == 1;
	o->paused = paused == 1;
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

bool usbpower_resume(const usb_power *p, bool kept, unsigned since_start_ms)
{
	return kept && p->known && p->ask && p->plug == 1 && p->pin == 1 && p->vbus == 0 &&
	       !p->computer && !p->automatic && since_start_ms < USBPOWER_RESUME_MS;
}

bool usbpower_takes_input(unsigned opened_ms, unsigned now_ms)
{
	return now_ms - opened_ms >= USBPOWER_TAKES_MS;
}

bool usbpower_mode(const char *dir, bool automatic)
{
	char tmp[256], path[256];
	const char *text = automatic ? "auto\n" : "ask\n";
	int fd;
	bool ok;

	snprintf(tmp, sizeof tmp, "%s/mode.new", dir);
	snprintf(path, sizeof path, "%s/mode", dir);
	if ((fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600)) < 0)
		return false;
	ok = write(fd, text, strlen(text)) == (ssize_t)strlen(text);
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

int usbpower_follow(usbpower_follow_state *f, const usb_power *p, unsigned now, unsigned *plug)
{
	/* A computer on the bus during this plug, or a plug that came back
	 * while one paused Automatic (the Mac's port dropping and coming back
	 * when its power went, 2026-11-10) */
	bool replug = usbpower_waits(p) && (p->computer || p->paused) && p->plug;

	*plug = 0;
	/* A computer on the bus, waiting: said at once, once */
	if (replug && f->told != p->plug) {
		f->told = p->plug;
		*plug = p->plug;
		return USBPOWER_REPLUG;
	}
	if (!p->known || !p->ask || !p->plug || (p->automatic && !p->paused)) {
		f->since = 0;
		return USBPOWER_NOTHING;
	}
	if (p->plug != f->plug) {
		f->plug = p->plug;
		f->since = 0;
	}
	if (!f->since) f->since = now ? now : 1;
	if (f->asked == p->plug || now - f->since < USBPOWER_ASK_MS) return USBPOWER_NOTHING;
	f->asked = p->plug;
	*plug = p->plug;
	return USBPOWER_ASK;
}
