/* The player's look (src/look.h, DESIGN.md 3.5): every accent with every
 * background tone, in both themes, held to the contrast DESIGN.md 3.2 owes
 * each color, as tools/designdef.py holds the tokens.
 *
 * Ten accents, three tones a theme: sixty themes, each worked out by
 * look_theme from an accent and a ground. In each, the text roles are owed
 * 4.5:1 on the ground and the surface, the accent's ink the same and 3:1 on
 * the track, the text on the fill 4.5:1, a warning 4.5:1 on the surface and
 * 3:1 on the ground, the heart and the light 3:1 on what they are drawn on,
 * and Storage's three parts 3:1 on the ground. And the look as it ships is
 * the tokens, to the byte; a look's words read back as the look; the ones
 * from another release read as it ships; and theme_at follows look_set.
 *
 * Links src/look.c alone, and no SDL. */
#include "../src/design.h"
#include "../src/look.h"
#include "../src/theme.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

static float lowest = 99.0f;
static char lowest_at[160];

/* `c` owed `need` on `on`, named for the report */
static void owe(ds_rgb c, ds_rgb on, float need, const char *what, const char *where)
{
	char m[200];
	float r = look_ratio(c, on);

	if (r < lowest) {
		lowest = r;
		snprintf(lowest_at, sizeof lowest_at, "%s, %s", what, where);
	}
	if (r + 1e-4f < need) {
		snprintf(m, sizeof m, "%s is %.2f:1, owed %.1f: %s", what, r, need, where);
		ck(0, m);
	}
}

static int same(const ds_theme *a, const ds_theme *b)
{
	return !memcmp(&a->ground, &b->ground, sizeof(ds_rgb) * 18) &&
	       a->scrim_menu == b->scrim_menu && a->scrim_game == b->scrim_game;
}

int main(void)
{
	int t, ac, tn, n = 0;
	ds_theme th;
	look l;

	for (t = 0; t < DS_THEMES; t++)
		for (ac = 0; ac < DS_ACCENTS; ac++)
			for (tn = 0; tn < DS_TONES; tn++) {
				char where[96];

				snprintf(where, sizeof where, "%s, accent %s, tone %s",
				         t == DS_LIGHT ? "Beard" : "Salt & Pepper",
				         DS_ACCENT_ID[ac], DS_TONE_ID[t][tn]);
				look_theme(t, DS_TONE[t][tn], DS_ACCENT[ac], &th);
				n++;
				owe(th.text, th.ground, 4.5f, "text on ground", where);
				owe(th.text, th.surface, 4.5f, "text on surface", where);
				owe(th.text_soft, th.ground, 4.5f, "text-soft on ground", where);
				owe(th.text_soft, th.surface, 4.5f, "text-soft on surface", where);
				owe(th.text_dim, th.ground, 4.5f, "text-dim on ground", where);
				owe(th.text_dim, th.surface, 4.5f, "text-dim on surface", where);
				owe(th.accent_ink, th.ground, 4.5f, "accent-ink on ground", where);
				owe(th.accent_ink, th.surface, 4.5f, "accent-ink on surface", where);
				owe(th.accent_ink, th.track, 3.0f, "accent-ink on track", where);
				owe(th.on_accent, th.accent, 4.5f, "on-accent on accent", where);
				owe(th.warning, th.surface, 4.5f, "warning on surface", where);
				owe(th.warning, th.ground, 3.0f, "warning on ground", where);
				owe(th.favorite, th.surface, 3.0f, "favorite on surface", where);
				owe(th.light, th.surface, 3.0f, "light on surface", where);
				owe(th.light, th.track, 3.0f, "light on track", where);
				owe(th.store_games, th.ground, 3.0f, "store-games on ground", where);
				owe(th.store_saves, th.ground, 3.0f, "store-saves on ground", where);
				owe(th.store_art, th.ground, 3.0f, "store-art on ground", where);
				owe(th.accent, th.ink, 3.0f, "accent on ink", where);
				ck(!memcmp(&th.accent, &DS_ACCENT[ac], sizeof th.accent),
				   "the fill is the accent picked");
				ck(!memcmp(&th.ground, &DS_TONE[t][tn], sizeof th.ground),
				   "the ground is the tone picked");
			}

	/* As it ships: the tokens, to the byte */
	for (t = 0; t < DS_THEMES; t++) {
		look_theme(t, DS_THEME[t].ground, DS_THEME[t].accent, &th);
		ck(same(&th, &DS_THEME[t]), "the first accent and tone are the theme as it ships");
	}
	l = LOOK_DEFAULT;
	look_set(&l);
	ck(same(theme_at(DS_LIGHT), &DS_THEME[DS_LIGHT]) && same(theme_at(DS_DARK), &DS_THEME[DS_DARK]),
	   "theme_at is the tokens with the look as it ships");
	ck(look_is_default(&l) && !look_square() && !look_zyper_off(), "the look as it ships");

	/* A look set is what theme_at answers, both themes */
	l = look_parse("blue", "cool", "deepblue", "square", "off");
	ck(l.accent == 3 && l.tone[DS_LIGHT] == 2 && l.tone[DS_DARK] == 1 && l.square && l.zyper_off,
	   "a look's words read back as the look");
	look_set(&l);
	ck(!memcmp(&theme_at(DS_LIGHT)->accent, &DS_ACCENT[3], 3) &&
	   !memcmp(&theme_at(DS_DARK)->ground, &DS_TONE[DS_DARK][1], 3),
	   "theme_at follows look_set");
	ck(look_square() && look_zyper_off(), "corners and Zyper follow look_set");
	l = look_parse("magenta", "sepia", NULL, "round", "maybe");
	ck(look_is_default(&l) && !l.square && !l.zyper_off,
	   "words from another release read as it ships");
	l = LOOK_DEFAULT;
	look_set(&l);

	if (fails) {
		printf("look: %d failed\n", fails);
		return 1;
	}
	printf("look: %d themes (%d accents by %d tones, both themes) meet every contrast they are owed;"
	       " the lowest, %.2f:1, is %s; as it ships is the tokens\n",
	       n, DS_ACCENTS, DS_TONES, lowest, lowest_at);
	return 0;
}
