/* SPDX-License-Identifier: GPL-3.0-only */
/* src/webtheme.h: the handheld's theme as the play page's theme.css.
 *
 * The page looks the same with the file and without it when nothing is
 * customised, so the check reads res/web/play/play.css (the generated token
 * blocks, which make check-design holds to DESIGN.md) and holds the file's
 * values to the ones the page already has, for every property the comment at
 * the top of play.css says theme.css may set, and for no other. Then a custom
 * look moves the accent, Square makes --corner 0, dark says color-scheme
 * dark, a buffer too small is refused and webtheme_write leaves a whole file.
 *
 * `webtheme-check --emit PATH` writes a custom dark, square file with a
 * magenta accent and does nothing else: tools/play-check.py serves it to
 * prove the page and the writer together.
 *
 * Run from the repository root. Links src/look.c and src/webtheme.c, no SDL. */
#include "../src/design.h"
#include "../src/look.h"
#include "../src/theme.h"
#include "../src/webtheme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* Every property play.css lets theme.css set: the roles, each also as -rgb */
static const char *const ROLES[] = {
	"ground", "surface", "raised", "edge", "track", "text", "text-soft", "text-dim",
	"accent", "on-accent", "accent-ink", "warning", "ink",
};
#define NROLES ((int)(sizeof ROLES / sizeof ROLES[0]))
/* and these, which are not colors */
static const char *const OTHER[] = { "panel-lift", "small-lift", "corner", "color-scheme" };
#define NOTHER ((int)(sizeof OTHER / sizeof OTHER[0]))
#define NPROPS (NROLES * 2 + NOTHER)

static char *slurp(const char *path)
{
	FILE *f = fopen(path, "rb");
	long n;
	char *b;

	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	rewind(f);
	b = calloc(1, (size_t)n + 1);
	if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
	fclose(f);
	return b;
}

/* The value of `--name` (or `name`) in the text from `from` on, up to its ;
 * and no further than `to`; into out, true when it is there */
static bool prop_in(const char *from, const char *to, const char *name, char *out, size_t cap)
{
	size_t nl = strlen(name);
	const char *p = from;

	while ((p = strstr(p, name)) && (!to || p < to)) {
		const char *v = p + nl;

		if ((p == from || p[-1] == '\t' || p[-1] == ' ' || p[-1] == '\n') && *v == ':') {
			const char *e = strchr(v, ';');
			size_t n;

			v++;
			while (*v == ' ') v++;
			n = (size_t)(e - v);
			if (!e || n >= cap) return false;
			memcpy(out, v, n);
			out[n] = 0;
			return true;
		}
		p += nl;
	}
	return false;
}

static const char *g_css, *g_light, *g_dark, *g_end;

/* The text of the file's own value for `prop` (without the --) */
static bool file_value(const char *file, const char *prop, char *out, size_t cap)
{
	char key[64];

	snprintf(key, sizeof key, "%s", prop);
	return prop_in(file, NULL, key, out, cap);
}

static void locate(void)
{
	g_css = slurp("res/web/play/play.css");
	if (!g_css) { printf("FAIL: res/web/play/play.css is not here (run from the repository root)\n"); exit(1); }
	g_light = strstr(g_css, "/* generated:tokens */");
	g_dark = g_light ? strstr(g_light, "@media (prefers-color-scheme: dark)") : NULL;
	g_end = g_dark ? strstr(g_dark, "/* /generated:tokens */") : NULL;
	if (!g_end) { printf("FAIL: play.css has no token blocks\n"); exit(1); }
}

/* Every property of the file is what the page's own block for `dark` has */
static void same_as_page(const ds_theme *t, bool dark, const char *what)
{
	char css[4096], want[160], got[160], name[64];
	const char *from = dark ? g_dark : g_light, *to = dark ? g_end : g_dark;
	int i;

	CHECK(webtheme_css(t, dark, false, css, sizeof css) > 0);
	for (i = 0; i < NROLES * 2 + 2; i++) {
		if (i < NROLES * 2) snprintf(name, sizeof name, i % 2 ? "--%s-rgb" : "--%s", ROLES[i / 2]);
		else snprintf(name, sizeof name, "--%s", OTHER[i - NROLES * 2]);
		if (!prop_in(from, to, name, want, sizeof want)) {
			printf("FAIL %s: play.css has no %s\n", what, name);
			fails++;
			continue;
		}
		if (!file_value(css, name, got, sizeof got) || strcmp(got, want)) {
			printf("FAIL %s: %s is '%s', the page has '%s'\n", what, name,
			       file_value(css, name, got, sizeof got) ? got : "(missing)", want);
			fails++;
		}
	}
	/* the corners are the page's own default, 1, and the scheme is the one asked */
	CHECK(prop_in(g_css, NULL, "--corner", want, sizeof want) && !strcmp(want, "1"));
	CHECK(file_value(css, "--corner", got, sizeof got) && !strcmp(got, "1"));
	CHECK(file_value(css, "color-scheme", got, sizeof got) && !strcmp(got, dark ? "dark" : "light"));
}

/* Which properties a file sets, counted by its declarations: the listed ones
 * once each and nothing else */
static void only_the_list(const char *css)
{
	char name[64];
	int seen = 0, i;
	const char *p = css;

	while ((p = strchr(p, '\n'))) {
		const char *a = ++p;
		const char *c;
		bool known = false;

		if (*a != '\t') continue;
		a++;
		c = strchr(a, ':');
		if (!c || c - a >= (int)sizeof name) { CHECK(!"a declaration with no name"); continue; }
		memcpy(name, a, (size_t)(c - a));
		name[c - a] = 0;
		seen++;
		for (i = 0; i < NROLES * 2 && !known; i++) {
			char want[64];

			snprintf(want, sizeof want, i % 2 ? "--%s-rgb" : "--%s", ROLES[i / 2]);
			known = !strcmp(name, want);
		}
		for (i = 0; i < NOTHER && !known; i++) {
			char want[64];

			snprintf(want, sizeof want, i + 1 < NOTHER ? "--%s" : "%s", OTHER[i]);
			known = !strcmp(name, want);
		}
		if (!known) { printf("FAIL: the file sets %s, which play.css does not list\n", name); fails++; }
	}
	CHECK(seen == NPROPS);
	CHECK(strstr(css, ":root"));
}

static void defaults_are_the_page(void)
{
	same_as_page(&DS_THEME[DS_LIGHT], false, "Beard");
	same_as_page(&DS_THEME[DS_DARK], true, "Salt & Pepper");
}

static void every_property_and_no_other(void)
{
	char css[4096];

	CHECK(webtheme_css(&DS_THEME[DS_LIGHT], false, false, css, sizeof css) > 0);
	only_the_list(css);
	CHECK(webtheme_css(&DS_THEME[DS_DARK], true, true, css, sizeof css) > 0);
	only_the_list(css);
}

static void length_is_returned(void)
{
	char css[4096];
	size_t n = webtheme_css(&DS_THEME[DS_LIGHT], false, false, css, sizeof css);

	CHECK(n > 0 && n == strlen(css));
}

static void a_custom_look_moves_the_accent(void)
{
	ds_theme t;
	char base[4096], css[4096], got[160];
	ds_rgb magenta = { 255, 0, 255 };

	look_theme(DS_DARK, DS_TONE[DS_DARK][1], magenta, &t);
	CHECK(webtheme_css(&DS_THEME[DS_DARK], true, false, base, sizeof base) > 0);
	CHECK(webtheme_css(&t, true, false, css, sizeof css) > 0);
	CHECK(file_value(css, "--accent", got, sizeof got) && !strcmp(got, "#ff00ff"));
	CHECK(file_value(css, "--accent-rgb", got, sizeof got) && !strcmp(got, "255, 0, 255"));
	CHECK(strcmp(base, css) != 0);
	/* the deep blue tone moved the ground too */
	CHECK(file_value(css, "--ground", got, sizeof got) && !strcmp(got, "#0e1526"));
}

static void square_and_dark(void)
{
	char css[4096], got[160];

	CHECK(webtheme_css(&DS_THEME[DS_LIGHT], false, true, css, sizeof css) > 0);
	CHECK(file_value(css, "--corner", got, sizeof got) && !strcmp(got, "0"));
	CHECK(file_value(css, "color-scheme", got, sizeof got) && !strcmp(got, "light"));
	CHECK(webtheme_css(&DS_THEME[DS_DARK], true, false, css, sizeof css) > 0);
	CHECK(file_value(css, "color-scheme", got, sizeof got) && !strcmp(got, "dark"));
	CHECK(file_value(css, "--corner", got, sizeof got) && !strcmp(got, "1"));
}

static void a_small_buffer_is_refused(void)
{
	char css[4096], small[4096];
	size_t n = webtheme_css(&DS_THEME[DS_LIGHT], false, false, css, sizeof css), c;

	memset(small, 'x', sizeof small);
	CHECK(webtheme_css(&DS_THEME[DS_LIGHT], false, false, small, 10) == 0);
	CHECK(webtheme_css(&DS_THEME[DS_LIGHT], false, false, small, 0) == 0);
	CHECK(webtheme_css(&DS_THEME[DS_LIGHT], false, false, NULL, 0) == 0);
	/* one byte short of the file and its NUL, and exactly enough */
	CHECK(webtheme_css(&DS_THEME[DS_LIGHT], false, false, small, n) == 0);
	c = webtheme_css(&DS_THEME[DS_LIGHT], false, false, small, n + 1);
	CHECK(c == n && small[n] == 0);
}

static void write_leaves_a_whole_file(void)
{
	char dir[] = "/tmp/webtheme-check-XXXXXX", path[256], tmp[300], css[4096];
	char *got;

	if (!mkdtemp(dir)) { CHECK(!"mkdtemp"); return; }
	snprintf(path, sizeof path, "%s/theme.css", dir);
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	CHECK(webtheme_write(path, &DS_THEME[DS_LIGHT], false, false));
	got = slurp(path);
	CHECK(got && webtheme_css(&DS_THEME[DS_LIGHT], false, false, css, sizeof css) > 0 && !strcmp(got, css));
	free(got);
	/* a second write replaces it whole, and no temporary is left */
	CHECK(webtheme_write(path, &DS_THEME[DS_DARK], true, true));
	got = slurp(path);
	CHECK(got && webtheme_css(&DS_THEME[DS_DARK], true, true, css, sizeof css) > 0 && !strcmp(got, css));
	free(got);
	CHECK(access(tmp, F_OK) != 0);
	/* a folder that is not there is a failure, not a crash */
	snprintf(tmp, sizeof tmp, "%s/none/theme.css", dir);
	CHECK(!webtheme_write(tmp, &DS_THEME[DS_LIGHT], false, false));
	unlink(path);
	rmdir(dir);
}

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "--emit")) {
		ds_theme t;

		look_theme(DS_DARK, DS_TONE[DS_DARK][1], (ds_rgb){ 255, 0, 255 }, &t);
		return webtheme_write(argv[2], &t, true, true) ? 0 : 1;
	}
	locate();
	defaults_are_the_page();
	every_property_and_no_other();
	length_is_returned();
	a_custom_look_moves_the_accent();
	square_and_dark();
	a_small_buffer_is_refused();
	write_leaves_a_whole_file();
	if (fails) {
		printf("webtheme: %d check(s) failed\n", fails);
		return 1;
	}
	printf("webtheme: ok\n");
	return 0;
}
