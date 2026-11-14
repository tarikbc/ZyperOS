/* SPDX-License-Identifier: GPL-3.0-only */
/* The remap rules (src/remap.c) and the choices a system offers
 * (src/controls.c):
 *
 *     make check-remap
 *
 * No SDL, no device. */
#include <stdio.h>
#include <string.h>

#include "../src/controls.h"
#include "../src/i18n.h"
#include "../src/padglyph.h"
#include "../src/remap.h"

static int fails;

static void report(int ok, const char *name)
{
	printf("  %s %s\n", ok ? "PASS" : "FAIL", name);
	if (!ok) fails++;
}

#define DC_MAP "a:b,b:a,x:y,y:x,l1:l2,l2:l1,r1:r2,r2:r1"
#define N64_MAP "select:r2,r2:none,a:b,b:y,y:none,select+a:a,select+b:b,select+y:y"

static rm_table parsed(const char *spec)
{
	rm_table t;

	rm_identity(&t);
	if (!rm_parse(spec, &t)) printf("  (bad spec in the check: %s)\n", spec);
	return t;
}

static void parse_format_roundtrip(void)
{
	rm_table t = parsed(DC_MAP), u;
	char out[200];
	bool ok;

	rm_format(&t, out, sizeof out);
	ok = rm_parse(out, &u) && rm_equal(&t, &u);
	/* the same eight pairs, in the table's order */
	ok = ok && strlen(out) == strlen(DC_MAP) && strstr(out, "l1:l2") && strstr(out, "r2:r1") &&
	     strstr(out, "a:b") && strstr(out, "y:x");
	report(ok, "parse_format_roundtrip");
}

static void parse_rules(void)
{
	rm_table t;
	char out[100];
	bool ok;

	rm_identity(&t);
	ok = !rm_parse("a:zz", &t) && !rm_parse("a:b,menu:a", &t) && !rm_parse("a", &t) &&
	     !rm_parse("x:a~0", &t) && !rm_parse("x:a~31", &t) && !rm_parse("x:none~3", &t);
	t = parsed("a:b,a:x,x:a~3,y:none");
	ok = ok && t.b[RM_A].to == RT_X && t.b[RM_X].to == RT_A && t.b[RM_X].turbo == 3 &&
	     t.b[RM_Y].to == RM_NONE;
	rm_format(&t, out, sizeof out);
	ok = ok && !strcmp(out, "a:x,x:a~3,y:none");
	rm_identity(&t);
	rm_format(&t, out, sizeof out);
	ok = ok && !strcmp(out, "identity") && rm_parse("identity", &t) && rm_parse("", &t);
	report(ok, "parse_rules");
}

static void swap_y_cross(void)
{
	rm_table base, t;

	rm_identity(&base);
	rm_set(&t, &base, RM_Y, (rm_choice){ RT_B, 0 });
	report(t.b[RM_Y].to == RT_B && t.b[RM_B].to == RT_Y && t.b[RM_A].to == RT_A,
	       "swap_y_cross");
}

static void turbo_never_swaps(void)
{
	rm_table base, t;

	rm_identity(&base);
	rm_set(&t, &base, RM_Y, (rm_choice){ RT_B, 3 });
	report(t.b[RM_Y].to == RT_B && t.b[RM_Y].turbo == 3 && t.b[RM_B].to == RT_B &&
	       t.b[RM_B].turbo == 0, "turbo_never_swaps");
	rm_set(&t, &base, RM_Y, (rm_choice){ RM_NONE, 0 });
	report(t.b[RM_B].to == RT_B && t.b[RM_Y].to == RM_NONE, "none_never_swaps");
}

static void steps_do_not_pile(void)
{
	rm_table base, t;

	rm_identity(&base);
	rm_set(&t, &base, RM_Y, (rm_choice){ RT_B, 0 });
	rm_set(&t, &base, RM_Y, (rm_choice){ RT_X, 0 });
	report(t.b[RM_B].to == RT_B && t.b[RM_X].to == RT_Y && t.b[RM_Y].to == RT_X,
	       "steps_do_not_pile");
}

static void none_warns(void)
{
	rm_table def, t;
	int targets[RT_COUNT], n;
	bool ok;

	rm_identity(&def);
	rm_set(&t, &def, RM_B, (rm_choice){ RM_NONE, 0 });
	n = rm_unreached(&def, &t, targets, RT_COUNT);
	ok = n == 1 && targets[0] == RT_B;
	/* a turbo of B is a button for B (Y, which gave Y, is what is left) */
	rm_set(&t, &t, RM_Y, (rm_choice){ RT_B, 3 });
	n = rm_unreached(&def, &t, targets, RT_COUNT);
	report(ok && n == 1 && targets[0] == RT_Y, "none_warns");
}

static void store_equal_is_empty(void)
{
	rm_table def = parsed(DC_MAP);
	char out[100] = "x";

	rm_store(&def, &def, out, sizeof out);
	report(out[0] == '\0', "store_equal_is_empty");
}

static void store_load_roundtrip(void)
{
	rm_table def = parsed(DC_MAP), t, back;
	char out[200];
	bool ok;

	rm_set(&t, &def, RM_Y, (rm_choice){ RT_A, 0 });
	t.b[RM_R1] = (rm_choice){ RT_L1, 3 };
	t.b[RM_START] = (rm_choice){ RM_NONE, 0 };
	rm_store(&def, &t, out, sizeof out);
	ok = !strncmp(out, "1;", 2) && rm_load(&def, out, &back) && rm_equal(&t, &back);
	/* only what differs is kept */
	ok = ok && !strstr(out, "a:b") && strstr(out, "r1:l1~3") && strstr(out, "start:none");
	report(ok, "store_load_roundtrip");
}

static void stored_bad_version_falls_back(void)
{
	rm_table def = parsed(DC_MAP), out;

	memset(&out, 0, sizeof out);
	report(!rm_load(&def, "2;a:b", &out) && rm_equal(&out, &def),
	       "stored_bad_version_falls_back");
}

static void stored_bad_pair_falls_back(void)
{
	rm_table def = parsed(DC_MAP), out;
	bool ok;

	memset(&out, 0, sizeof out);
	ok = !rm_load(&def, "1;a:zz", &out) && rm_equal(&out, &def);
	ok = ok && !rm_load(&def, "1;x:a,a:zz", &out) && rm_equal(&out, &def);
	report(ok, "stored_bad_pair_falls_back");
}

static void n64_layer_survives_change(void)
{
	rm_table def = parsed(N64_MAP), t;
	char out[200];

	rm_set(&t, &def, RM_A, (rm_choice){ RT_X, 0 });
	rm_format(&t, out, sizeof out);
	report(strstr(out, "select+a:a,select+b:b,select+y:y") != NULL && t.b[RM_A].to == RT_X,
	       "n64_layer_survives_change");
}

static void letters_dc_place_identity(void)
{
	rm_table letters, t;
	bool ok;

	ok = ctl_letter_layout("DC", &letters);
	rm_identity(&t);
	rm_faces(&t, &letters);
	/* the letters: each cap the button it names, so the Dreamcast's A is the
	 * core's B (and so on) as map|DC has it */
	ok = ok && rm_faces_of(&t, &letters) == 0;
	rm_identity(&t);
	ok = ok && rm_faces_of(&t, &letters) == 1;
	rm_set(&t, &t, RM_A, (rm_choice){ RT_X, 0 });
	ok = ok && rm_faces_of(&t, &letters) == 2;
	rm_identity(&t);
	rm_faces(&t, &letters);
	ok = ok && t.b[RM_A].to == RT_B && t.b[RM_B].to == RT_A && t.b[RM_X].to == RT_Y &&
	     t.b[RM_Y].to == RT_X;
	report(ok, "letters_dc_place_identity");
}

static void letters_per_system(void)
{
	static const char *const yes[] = { "DC", "SATURN", "NGP", "NGPC" };
	static const char *const no[] = { "PS1", "PSP", "NAOMI", "NES", "GBA", "MD", "NEOGEO", "N64" };
	rm_table l;
	bool ok = true;
	size_t i;

	for (i = 0; i < sizeof yes / sizeof yes[0]; i++) {
		bool got = ctl_letter_layout(yes[i], &l);

		if (!got) printf("    %s should have a letter layout\n", yes[i]);
		ok = ok && got;
	}
	for (i = 0; i < sizeof no / sizeof no[0]; i++) {
		bool got = ctl_letter_layout(no[i], &l);

		if (got) printf("    %s should have none\n", no[i]);
		ok = ok && !got;
	}
	report(ok && !ctl_letter_layout("NOPE", &l), "letters_per_system");
}

static bool says(const char *tag, rm_choice c, const char *want)
{
	char s[64];

	ctl_remap_say(tag, NULL, c, s, sizeof s);
	if (strcmp(s, want)) printf("    %s said \"%s\", wanted \"%s\"\n", tag, s, want);
	return !strcmp(s, want);
}

static void choices_dc(void)
{
	static const char *const want[] = {
		"A", "B", "X", "Y", "L trigger", "R trigger", "Start",
		"Turbo A", "Turbo B", "Turbo X", "Turbo Y", "Nothing"
	};
	rm_choice c[24];
	int n = ctl_remap_choices("DC", NULL, DC_MAP, c, 24), i;
	bool ok = n == 12;

	for (i = 0; ok && i < n; i++) {
		char s[64];

		ctl_remap_say("DC", NULL, c[i], s, sizeof s);
		if (strcmp(s, want[i])) { printf("    %d: \"%s\" not \"%s\"\n", i, s, want[i]); ok = false; }
	}
	/* a short list is cut, not overrun */
	ok = ok && ctl_remap_choices("DC", NULL, DC_MAP, c, 5) == 5;
	ok = ok && c[11].to == RM_NONE;
	report(ok, "choices_dc");
}

static void choices_others(void)
{
	rm_choice c[24];
	int n = ctl_remap_choices("PS1", NULL, "", c, 24);
	/* the PlayStation: four faces, six others, four turbos, nothing */
	bool ok = n == 4 + 6 + 4 + 1;

	/* circle, cross, triangle, square first */
	ok = ok && c[0].to == RT_A && c[1].to == RT_B && c[2].to == RT_X && c[3].to == RT_Y;
	ok = ok && says("PS1", c[1], PAD_CROSS) && says("PS1", (rm_choice){ RT_B, 3 }, "Turbo " PAD_CROSS);
	ok = ok && ctl_remap_choices("NOPE", NULL, "", c, 24) == 0;
	/* the N64: its cap A gives the core's B, which the N64 calls A */
	n = ctl_remap_choices("N64", NULL, N64_MAP, c, 24);
	ok = ok && n > 0 && says("N64", c[0], "A") && says("N64", c[1], "B");
	report(ok, "choices_others");
}

int main(void)
{
	printf("remap: the rules\n");
	parse_format_roundtrip();
	parse_rules();
	swap_y_cross();
	turbo_never_swaps();
	steps_do_not_pile();
	none_warns();
	store_equal_is_empty();
	store_load_roundtrip();
	stored_bad_version_falls_back();
	stored_bad_pair_falls_back();
	n64_layer_survives_change();
	letters_dc_place_identity();
	letters_per_system();
	choices_dc();
	choices_others();
	if (fails) {
		printf("\n%d failure%s\n", fails, fails == 1 ? "" : "s");
		return 1;
	}
	printf("ok\n");
	return 0;
}
