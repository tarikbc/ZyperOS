/* systems.cfg's accent field: a color, or no color at all.
 *
 * A system's line names its color as six hex digits. One that names none -
 * the field empty, left off the end of the line, or holding something that is
 * not a color - used to come out cyan, which DESIGN.md 3.3 takes out of the
 * system, and a typo came out black. Now it is CFG_NO_COLOR, which no line
 * can name, and the screens draw what nothing looks like by role: text-dim
 * for a mark (src/ui.h, ui_sys_mark). This holds the reading side of that:
 * which fields are a color and which are not, and that "not" can never be
 * mistaken for a color a line could have named.
 *
 * And the lines that add to a system by its tag - power, map, controller -
 * and the RUN keys made from them (cfg_run_keys).
 *
 * Links src/config.c alone, and NOT SDL. Writes its one file under a scratch
 * directory of its own.
 */
#include "../src/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

/* The system named `name` in what was read, or NULL. */
static const system_cfg *find(const systems_cfg *c, const char *name)
{
	int i;

	for (i = 0; i < c->count; i++)
		if (!strcmp(c->systems[i].name, name)) return &c->systems[i];
	return NULL;
}

int main(void)
{
	static systems_cfg c;
	char dir[] = "/tmp/zyperos-config-XXXXXX", path[64];
	const system_cfg *s;
	FILE *f;

	if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
	snprintf(path, sizeof path, "%s/systems.cfg", dir);
	f = fopen(path, "w");
	if (!f) { perror(path); return 1; }
	fputs("# one line each way a color can be named, or not\n"
	      "sys|Named|named|core|NAM|N.png|C4443A|nes\n"
	      "sys|Lower|lower|core|LOW|L.png|8a6fc0|sfc\n"
	      "sys|Black|black|core|BLK|B.png|000000|bin\n"
	      "sys|Empty|empty|core|EMP|E.png||gb\n"
	      "sys|Spaces|spaces|core|SPC|S.png|   |gb\n"
	      "sys|Short|short|core|SHO|S.png\n"
	      "sys|Word|word|core|WRD|W.png|purple|gb\n"
	      "sys|Five|five|core|FIV|F.png|C4443|gb\n"
	      "sys|Seven|seven|core|SEV|S.png|C4443A0|gb\n"
	      "sys|Hash|hash|core|HSH|H.png|#C4443A|gb\n"
	      "# by tag, before and after the line they add to\n"
	      "map|NAM|a:b,b:a,x:y,y:x,l1:l2,l2:l1,r1:r2,r2:r1\n"
	      "power|NAM|medium\n"
	      "controller|LOW|517\n"
	      "map|NOPE|a:b\n"
	      "sys|Late|late|core|LAT|L.png|C4443A|gb\n"
	      "power|LAT|light\n"
	      "controller|LAT|517\n", f);
	fclose(f);

	printf("reading:\n");
	ck(cfg_load_systems(path, &c), "the file loads");
	ck(c.count == 11, "every sys line is a system, colored or not");

	printf("a color:\n");
	s = find(&c, "Named");
	ck(s && s->accent == 0xC4443Au, "six hex digits are that color");
	s = find(&c, "Lower");
	ck(s && s->accent == 0x8A6FC0u, "in either case");
	s = find(&c, "Black");
	ck(s && s->accent == 0x000000u, "black is a color, not the lack of one");

	printf("no color:\n");
	ck(CFG_NO_COLOR > 0xFFFFFFu, "CFG_NO_COLOR is no color a line can name");
	s = find(&c, "Empty");
	ck(s && s->accent == CFG_NO_COLOR, "an empty field names none");
	s = find(&c, "Spaces");
	ck(s && s->accent == CFG_NO_COLOR, "nor does one of spaces");
	s = find(&c, "Short");
	ck(s && s->accent == CFG_NO_COLOR, "nor a line that stops before it");
	s = find(&c, "Word");
	ck(s && s->accent == CFG_NO_COLOR, "a word is not a color");
	s = find(&c, "Five");
	ck(s && s->accent == CFG_NO_COLOR, "five digits are not one");
	s = find(&c, "Seven");
	ck(s && s->accent == CFG_NO_COLOR, "nor seven");
	s = find(&c, "Hash");
	ck(s && s->accent == CFG_NO_COLOR, "nor a web color's # (the file never had one)");

	printf("by tag:\n");
	{
		char keys[96];

		s = find(&c, "Named");
		ck(s && !strcmp(s->map, "a:b,b:a,x:y,y:x,l1:l2,l2:l1,r1:r2,r2:r1"),
		   "map| gives its system the map, whole");
		ck(s && !strcmp(s->power, "medium") && s->controller == 0,
		   "beside power|, and no controller unless one is named");
		s = find(&c, "Lower");
		ck(s && s->controller == 517 && !s->map[0] && !s->power[0],
		   "controller| gives the number, and nothing else");
		s = find(&c, "Late");
		ck(s && !strcmp(s->power, "light") && s->controller == 517,
		   "lines after the system they name still reach it");
		s = find(&c, "Black");
		ck(s && !s->map[0] && !s->power[0] && !s->controller,
		   "a system nobody names gets none, and a tag nobody has is dropped");

		cfg_run_keys(find(&c, "Black"), false, keys, (int)sizeof keys);
		ck(!strcmp(keys, ""), "no RUN keys for a plain system");
		cfg_run_keys(find(&c, "Lower"), false, keys, (int)sizeof keys);
		ck(!strcmp(keys, "controller=517"), "controller= alone");
		cfg_run_keys(find(&c, "Late"), false, keys, (int)sizeof keys);
		ck(!strcmp(keys, "power=light\tcontroller=517"), "power= then controller=");
		cfg_run_keys(find(&c, "Late"), true, keys, (int)sizeof keys);
		ck(!strcmp(keys, "screens=2\tframeskip=late\tpower=light\tcontroller=517"),
		   "two screens first, all tab separated");
		cfg_run_keys(find(&c, "Black"), true, keys, (int)sizeof keys);
		ck(!strcmp(keys, "screens=2\tframeskip=late"), "two screens alone");
	}

	unlink(path);
	rmdir(dir);

	/* A to Z (2026-11-06): case and accents do not count, numbers
	 * count as numbers, and the port shelf stays last */
	printf("A to Z:\n");
	ck(cfg_name_cmp("3DO", "32X") < 0, "3DO before 32X: three before thirty-two");
	ck(cfg_name_cmp("Atari 2600", "Atari 5200") < 0 && cfg_name_cmp("Atari 7800", "Atari Lynx") < 0,
	   "the Ataris by number, then Lynx");
	ck(cfg_name_cmp("MAME", "Master System") < 0 && cfg_name_cmp("Master System", "MSX2") < 0,
	   "capitals do not count: MAME, Master System, MSX2");
	ck(cfg_name_cmp("Pokémon Mini", "Ports") < 0 && cfg_name_cmp("PlayStation", "Pokémon Mini") < 0,
	   "an accent does not count: Pokémon between PlayStation and Ports");
	ck(cfg_name_cmp("Neo Geo", "Neo Geo CD") < 0, "a name before the longer one it starts");
	ck(cfg_name_cmp("Game & Watch", "Game Boy") < 0, "Game & Watch before Game Boy");
	ck(cfg_name_cmp("NES", "nes") == 0, "the same name in other capitals is the same");
	ck(cfg_name_cmp("Disc 02", "Disc 2") == 0 && cfg_name_cmp("Disc 9", "Disc 10") < 0,
	   "leading zeros do not count, and 9 before 10");
	{
		static const char *const AZ[] = {
			"3DO", "32X", "Arcade", "Atari 2600", "Atari 5200", "Atari 7800", "Atari Lynx",
			"Atomiswave", "ColecoVision", "Doom", "Dreamcast", "Game & Watch", "Game Boy",
			"Game Boy Advance",
			"Game Boy Color", "Game Gear", "Genesis", "J2ME", "MAME", "Master System",
			"MSX2", "NAOMI", "Neo Geo", "Neo Geo CD", "Neo Geo Pocket",
			"Neo Geo Pocket Color", "NES", "Nintendo 64", "Nintendo DS", "OpenBOR",
			"PICO-8", "PlayStation", "Pokémon Mini", "PSP", "Saturn", "ScummVM", "Sega CD",
			"SNES", "TIC-80", "TurboGrafx-16", "Vectrex", "Virtual Boy", "WonderSwan",
			"WonderSwan Color", "Ports",
		};
		int i, n = (int)(sizeof AZ / sizeof AZ[0]), same = 1;

		ck(cfg_load_systems("config/systems.cfg", &c), "config/systems.cfg loads");
		cfg_sort_systems(&c);
		ck(c.count == n, "every system in it, once");
		for (i = 0; i < n && i < c.count; i++)
			if (strcmp(c.systems[i].name, AZ[i])) {
				printf("    at %d: %s, wanted %s\n", i, c.systems[i].name, AZ[i]);
				same = 0;
			}
		ck(same, "the shipped systems in A to Z, Ports last");
		/* and the tags, which settings hang off, went with their names */
		ck(!strcmp(c.systems[0].tag, "3DO") && !strcmp(c.systems[n - 1].tag, "PRT"),
		   "each line moves whole");
	}

	if (fails) { printf("config: %d FAILED\n", fails); return 1; }
	printf("config: ok\n");
	return 0;
}
