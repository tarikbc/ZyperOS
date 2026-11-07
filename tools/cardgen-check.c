/* The card drawn where there is no art, in each theme, without a screen.
 *
 * Two rules, each of which fails quietly on a handheld. The title has to read
 * on the slab at every row, in both themes: the card was a dark slab with
 * Salt & Pepper's beard-white title in either theme until the themes came,
 * and Beard's slab is light, so a title color that forgot which theme it was
 * in would be white on near-white, a card with no name on it. And a card is
 * shown only in the theme it was made for: made in Beard and kept after Auto's
 * 19:00, it is a light card on a dark shelf, and nobody would know why the
 * games with no art were the only things that did not change.
 *
 * Contrast is WCAG 2's, as tools/designdef.py measures it, and the title is
 * owed DESIGN.md's 4.5:1 against every row of the slab. Beside that, the slab
 * is the theme's surface at the top and never reaches the ground, so the foot
 * of a card stands off the shelf it is on.
 *
 * Links src/cardgen.c and NOT SDL.
 */
#include "../src/cardgen.h"
#include "../src/design.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

static double chan(uint8_t v)
{
	double c = v / 255.0;
	return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

static double lum(ds_rgb c)
{
	return 0.2126 * chan(c.r) + 0.7152 * chan(c.g) + 0.0722 * chan(c.b);
}

static double ratio(ds_rgb a, ds_rgb b)
{
	double la = lum(a), lb = lum(b);
	return ((la > lb ? la : lb) + 0.05) / ((la > lb ? lb : la) + 0.05);
}

static int same(ds_rgb a, ds_rgb b)
{
	return a.r == b.r && a.g == b.g && a.b == b.b;
}

/* Every row a card is drawn with: card_surface asks for y / height, and the
 * tallest card is 656 rows. */
#define ROWS 656

int main(void)
{
	int t, y;

	for (t = 0; t < DS_THEMES; t++) {
		const ds_theme *th = &DS_THEME[t];
		ds_rgb title = cardgen_title(t), top = cardgen_slab(t, 0.0f),
		       foot = cardgen_slab(t, 1.0f);
		double worst = 1e9, ground = lum(th->ground);
		int reads = 1, off = 1, falls = 1;
		char what[160];

		printf("%s:\n", th->name);
		ck(same(title, th->text), "the title is the theme's text");
		ck(same(top, th->surface), "the slab is the theme's surface at the top");
		for (y = 0; y <= ROWS; y++) {
			ds_rgb s = cardgen_slab(t, (float)y / ROWS);
			double c = ratio(title, s);

			if (c < worst) worst = c;
			if (c < 4.5) reads = 0;
			/* Short of the ground: as far from it as the surface is, in the
			 * same direction, and never the ground itself */
			if (same(s, th->ground) ||
			    (lum(s) - ground) * (lum(th->surface) - ground) <= 0.0) off = 0;
			/* And darker at the foot, row on row, never lighter */
			if (y && lum(s) > lum(cardgen_slab(t, (float)(y - 1) / ROWS))) falls = 0;
		}
		snprintf(what, sizeof what,
		         "the title reads on every row of the slab (worst %.1f:1, owed 4.5:1)",
		         worst);
		ck(reads, what);
		ck(off, "no row of the slab reaches the ground");
		ck(falls, "the slab darkens toward the foot and never lightens");
		ck(lum(foot) < lum(top), "the foot is darker than the top");
		ck(same(cardgen_slab(t, -1.0f), top) && same(cardgen_slab(t, 2.0f), foot),
		   "a row past either end is that end");
	}

	/* Beard's slab is the light one, Review Focus 3 of the themes plan: a
	 * charcoal title on a slab lighter than the ground, not the old dark card
	 * carried over */
	printf("Beard's card is light:\n");
	ck(lum(cardgen_slab(DS_LIGHT, 1.0f)) > lum(DS_THEME[DS_LIGHT].ground),
	   "its foot is lighter than Beard's ground");
	ck(lum(cardgen_title(DS_LIGHT)) < lum(cardgen_slab(DS_LIGHT, 1.0f)),
	   "and its title is darker than its slab");
	ck(lum(cardgen_title(DS_DARK)) > lum(cardgen_slab(DS_DARK, 0.0f)),
	   "Salt & Pepper's title is lighter than its slab");
	ck(same(cardgen_slab(7, 0.5f), cardgen_slab(DS_LIGHT, 0.5f)) &&
	   same(cardgen_title(-3), cardgen_title(DS_LIGHT)),
	   "a theme that is neither is Beard");

	printf("which card is kept:\n");
	ck(cardgen_kept(DS_LIGHT, DS_LIGHT), "a card made in Beard is kept in Beard");
	ck(cardgen_kept(DS_DARK, DS_DARK), "a card made in Salt & Pepper is kept there");
	ck(!cardgen_kept(DS_LIGHT, DS_DARK), "a card made in Beard is dropped in Salt & Pepper");
	ck(!cardgen_kept(DS_DARK, DS_LIGHT), "a card made in Salt & Pepper is dropped in Beard");
	ck(cardgen_kept(-1, DS_LIGHT) && cardgen_kept(-1, DS_DARK),
	   "art, which is no theme's, is kept in both");

	if (fails) { printf("cardgen: %d FAILED\n", fails); return 1; }
	printf("cardgen: ok\n");
	return 0;
}
