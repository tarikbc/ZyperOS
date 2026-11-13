/* SPDX-License-Identifier: GPL-3.0-only */
/* The play page's theme.css. See src/webtheme.h. */
#include "webtheme.h"

#include <stdarg.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

/* Appends printf text at *at, false when it would not fit in cap */
static bool put(char *out, size_t cap, size_t *at, const char *fmt, ...) __attribute__((format(printf, 4, 5)));

static bool put(char *out, size_t cap, size_t *at, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(out + *at, cap - *at, fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t)n >= cap - *at) return false;
	*at += (size_t)n;
	return true;
}

size_t webtheme_css(const ds_theme *t, bool dark, bool square, char *out, size_t cap)
{
	/* The roles play.css lets this set, in its own order */
	const struct { const char *name; ds_rgb c; } role[] = {
		{ "ground", t->ground }, { "surface", t->surface }, { "raised", t->raised },
		{ "edge", t->edge }, { "track", t->track }, { "text", t->text },
		{ "text-soft", t->text_soft }, { "text-dim", t->text_dim },
		{ "accent", t->accent }, { "on-accent", t->on_accent },
		{ "accent-ink", t->accent_ink }, { "warning", t->warning }, { "ink", t->ink },
	};
	size_t at = 0, i;

	if (!t || !out || !cap) return 0;
	if (!put(out, cap, &at,
	         "/* SPDX-License-Identifier: GPL-3.0-only */\n"
	         "/* The handheld's theme (src/webtheme.c), written by the launcher whenever\n"
	         " * it changes: the properties res/web/play/play.css says this may set. */\n"
	         ":root {\n\tcolor-scheme: %s;\n", dark ? "dark" : "light"))
		return 0;
	for (i = 0; i < sizeof role / sizeof role[0]; i++) {
		ds_rgb c = role[i].c;

		if (!put(out, cap, &at, "\t--%s: #%02x%02x%02x;\n\t--%s-rgb: %d, %d, %d;\n",
		         role[i].name, c.r, c.g, c.b, role[i].name, c.r, c.g, c.b))
			return 0;
	}
	/* The lifts are the page's own for the theme, which play.css makes from
	 * DESIGN.md 7 (tools/designdef.py); check-webtheme holds these to its
	 * blocks. Light's lift is a shadow of the ink, dark's a glow of the dim
	 * text, so both follow the colors set above. */
	if (!put(out, cap, &at, "\t--panel-lift: %s;\n\t--small-lift: %s;\n\t--corner: %d;\n}\n",
	         dark ? "0 0 105px rgba(var(--text-dim-rgb), 0.017)"
	              : "0 24px 64px rgba(var(--ink-rgb), 0.18)",
	         dark ? "none" : "0 8px 20px rgba(var(--ink-rgb), 0.18)",
	         square ? 0 : 1))
		return 0;
	return at;
}

bool webtheme_write(const char *path, const ds_theme *t, bool dark, bool square)
{
	char css[2048], tmp[1024];
	size_t len = webtheme_css(t, dark, square, css, sizeof css);
	bool ok;
	int fd;

	if (!path || !len) return false;
	if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return false;
	/* As src/themefile.c: the temporary is ours alone (a link left at its
	 * name in /tmp must not be written through), then renamed over the file,
	 * and p2d reads the old one whole or the new one whole */
	unlink(tmp);
	fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
	if (fd < 0) return false;
	ok = write(fd, css, len) == (ssize_t)len;
	if (close(fd) != 0) ok = false;
	if (ok && rename(tmp, path) != 0) ok = false;
	if (!ok) unlink(tmp);
	return ok;
}
