/* A system's color as a mark, in each theme, without a screen.
 *
 *     make check-sysmark
 *
 * Every mark of a system's color - the rail's segment, a menu's heading rule,
 * a save slot's dot, the rule under a generated card's title - goes through
 * one rule (src/sysmark.c, which src/ui.c's ui_sys_mark_in calls), DESIGN.md
 * 3.3: the color as systems.cfg has it in Salt & Pepper, a quarter of the way
 * toward **text** on Beard, and two colors said by role instead. Each part of
 * it fails where nobody is looking for it:
 *
 *   a system whose line names no color drawn in some color all the same is a
 *   system that looks like it belongs to another;
 *   lime on Beard's ground is 1.3:1, so Muse's and Favorites' rail would all
 *   but vanish on the light shelf, where accent-ink reads;
 *   a yellow like Game Boy Color's, unmixed on Beard, is a mark nobody can
 *   find, and one mixed on Salt & Pepper is a system's color it does not
 *   have.
 *
 * The system colors are the shipped ones, read from config/systems.cfg, so a
 * system added there is held to the rule with the rest.
 *
 * `sysmark-check --table [systems.cfg]` checks nothing and prints the rule's
 * answer instead, for every row of systems.cfg that names a card and for the
 * launcher's own two shelves, in both themes, one line each:
 *
 *     band|CARD.png|RRGGBB in Salt & Pepper|RRGGBB in Beard
 *
 * It is the one other copy of this rule that is not C. tools/genplainjane.py
 * draws a Plain Jane card's band in the system's color as a mark (its
 * `sysmark`), a Python copy of src/sysmark.c, and tools/cards-check.py reads
 * this table (make check-cards) and fails a card whose band is any other
 * color, so the copy cannot drift from the rule without a check saying so.
 *
 * Links src/sysmark.c and src/config.c, and NOT SDL.
 */
#include "../src/config.h"
#include "../src/design.h"
#include "../src/sysmark.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

static int same(ds_rgb a, ds_rgb b)
{
	return a.r == b.r && a.g == b.g && a.b == b.b;
}

static unsigned hex(ds_rgb c)
{
	return (unsigned)c.r << 16 | (unsigned)c.g << 8 | c.b;
}

/* A quarter of the way from `c` to `to`, to within the one unit a channel can
 * lose to being a whole number. */
static int quarter_toward(ds_rgb got, unsigned c, ds_rgb to)
{
	const int ch[3] = { (int)(c >> 16 & 255), (int)(c >> 8 & 255), (int)(c & 255) };
	const int t[3] = { to.r, to.g, to.b }, g[3] = { got.r, got.g, got.b };
	int i;

	for (i = 0; i < 3; i++) {
		double want = ch[i] + (t[i] - ch[i]) * 0.25;

		if (g[i] < want - 1.0 || g[i] > want + 1.0) return 0;
	}
	return 1;
}

static void by_role(void)
{
	int t;
	char msg[160];

	printf("two colors are said by role\n");
	for (t = 0; t < DS_THEMES; t++) {
		const ds_theme *th = &DS_THEME[t];

		snprintf(msg, sizeof msg, "%s: no color is text-dim", th->name);
		ck(same(sysmark(CFG_NO_COLOR, t), th->text_dim), msg);
		snprintf(msg, sizeof msg, "%s: the accent, lime, is accent-ink", th->name);
		ck(same(sysmark(hex(th->accent), t), th->accent_ink), msg);
	}
}

static void systems(const char *path)
{
	systems_cfg *c = calloc(1, sizeof *c);
	char msg[192];
	int i, colored = 0;

	printf("a system's own color, from %s\n", path);
	if (!c) { ck(0, "memory"); return; }
	ck(cfg_load_systems(path, c), "the file loads");
	ck(c->count > 10, "and names the systems");
	for (i = 0; i < c->count; i++) {
		const system_cfg *s = &c->systems[i];
		unsigned rgb = s->accent;
		ds_rgb dark, light;

		if (rgb == CFG_NO_COLOR || rgb == hex(DS_THEME[DS_LIGHT].accent)) continue;
		colored++;
		dark = sysmark(rgb, DS_DARK);
		light = sysmark(rgb, DS_LIGHT);
		snprintf(msg, sizeof msg, "%s (%06X): as it is on Salt & Pepper, got %06X",
		         s->name, rgb, hex(dark));
		ck(hex(dark) == rgb, msg);
		snprintf(msg, sizeof msg, "%s (%06X): a quarter toward text on Beard, got %06X",
		         s->name, rgb, hex(light));
		ck(quarter_toward(light, rgb, DS_THEME[DS_LIGHT].text), msg);
		snprintf(msg, sizeof msg, "%s: a theme that is neither is Beard", s->name);
		ck(same(sysmark(rgb, 7), light) && same(sysmark(rgb, -1), light), msg);
	}
	ck(colored > 10, "and most of them have a color of their own");
	free(c);
}

/* The ends of the range, where a mix that is off by one way or the other
 * shows: white and black a quarter of the way to Beard's charcoal text. */
static void ends(void)
{
	ds_rgb w = sysmark(0xFFFFFFu, DS_LIGHT), k = sysmark(0x000000u, DS_LIGHT);

	printf("the ends of the range\n");
	ck(quarter_toward(w, 0xFFFFFFu, DS_THEME[DS_LIGHT].text), "white on Beard");
	ck(quarter_toward(k, 0x000000u, DS_THEME[DS_LIGHT].text), "black on Beard");
	ck(hex(sysmark(0xFFFFFFu, DS_DARK)) == 0xFFFFFFu &&
	   hex(sysmark(0x000000u, DS_DARK)) == 0x000000u, "and both as they are on Salt & Pepper");
}

/* The band table: the mark of every card's color in each theme, for
 * tools/cards-check.py. Favorites and Muse have no color of their own and are
 * given ZYPEROS_ACCENT (src/main.c), the theme's accent, which sysmark turns
 * into accent-ink. */
static int band_table(const char *path)
{
	systems_cfg *c = calloc(1, sizeof *c);
	int i;

	if (!c) return 1;
	if (!cfg_load_systems(path, c)) {
		fprintf(stderr, "sysmark-check: %s does not load\n", path);
		free(c);
		return 1;
	}
	printf("# band|card|Salt & Pepper|Beard: src/sysmark.c, step for step\n");
	for (i = 0; i < c->count; i++) {
		const system_cfg *s = &c->systems[i];

		if (!s->card[0]) continue;
		printf("band|%s|%06X|%06X\n", s->card,
		       hex(sysmark(s->accent, DS_DARK)), hex(sysmark(s->accent, DS_LIGHT)));
	}
	printf("band|FAVORITES.png|%06X|%06X\n",
	       hex(sysmark(hex(DS_THEME[DS_DARK].accent), DS_DARK)),
	       hex(sysmark(hex(DS_THEME[DS_LIGHT].accent), DS_LIGHT)));
	printf("band|MUSE.png|%06X|%06X\n",
	       hex(sysmark(hex(DS_THEME[DS_DARK].accent), DS_DARK)),
	       hex(sysmark(hex(DS_THEME[DS_LIGHT].accent), DS_LIGHT)));
	free(c);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "--table"))
		return band_table(argc > 2 ? argv[2] : "config/systems.cfg");
	by_role();
	systems(argc > 1 ? argv[1] : "config/systems.cfg");
	ends();
	if (fails) { printf("\n%d FAILED\n", fails); return 1; }
	printf("\nok: a system's mark is its color, mixed on Beard, and two are roles\n");
	return 0;
}
