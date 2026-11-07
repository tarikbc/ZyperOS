/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* DraStic's .dsv into DSperate's .srm (src/dsv.c): the footer comes off, the
 * chip's bytes are kept exactly, an .srm already there is never touched, a
 * file that is not a .dsv is left alone, and the .srm goes in through
 * src/atomic.h's temporary, so a write that fails leaves none. Offline, in a
 * temporary folder. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/dsv.h"

static int fails;

static void ck(int ok, const char *what)
{
	printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok) fails++;
}

static void put(const char *path, const void *data, size_t n)
{
	FILE *f = fopen(path, "wb");
	fwrite(data, 1, n, f);
	fclose(f);
}

static long get(const char *path, unsigned char *buf, size_t cap)
{
	FILE *f = fopen(path, "rb");
	long n;

	if (!f) return -1;
	n = (long)fread(buf, 1, cap, f);
	fclose(f);
	return n;
}

int main(void)
{
	static const char snip[] = "|<--Snip above here to create a raw sav by excluding "
	                           "this DeSmuME savedata footer:";
	static unsigned char chip[8192], file[8192 + 122], back[16384];
	char dir[] = "/tmp/dsv-check-XXXXXX", p[256];
	unsigned int u[6] = { 8192, 8192, 0, 2, 8192, 0 };
	size_t i;

	if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
	for (i = 0; i < sizeof chip; i++) chip[i] = (unsigned char)(i * 7 + 3);
	/* New Super Mario Bros' shape, an 8 KB EEPROM, footer as ROGUE's has it. */
	memcpy(file, chip, sizeof chip);
	memcpy(file + 8192, snip, 82);
	memcpy(file + 8192 + 82, u, sizeof u);   /* little-endian, as on the device */
	memcpy(file + 8192 + 106, "|-DESMUME SAVE-|", 16);

	snprintf(p, sizeof p, "%s/Game (USA).dsv", dir);
	put(p, file, sizeof file);
	ck(dsv_import(dir, "Game (USA).nds"), "a .dsv with no .srm is taken over");
	snprintf(p, sizeof p, "%s/Game (USA).srm", dir);
	ck(get(p, back, sizeof back) == 8192 && !memcmp(back, chip, 8192),
	   "the .srm is the chip's 8192 bytes exactly, footer off");
	snprintf(p, sizeof p, "%s/Game (USA).srm.new", dir);
	ck(access(p, F_OK) != 0, "no temporary is left beside it");
	snprintf(p, sizeof p, "%s/Game (USA).srm", dir);

	put(p, "mine", 4);
	ck(!dsv_import(dir, "Game (USA).nds") && get(p, back, sizeof back) == 4,
	   "an .srm that is there is never written over");

	snprintf(p, sizeof p, "%s/Zipped.dsv", dir);
	put(p, file, sizeof file);
	ck(dsv_import(dir, "Zipped.zip"), "the game's name, whatever its extension");

	snprintf(p, sizeof p, "%s/Raw.dsv", dir);
	put(p, chip, sizeof chip);
	snprintf(p, sizeof p, "%s/Raw.srm", dir);
	ck(!dsv_import(dir, "Raw.nds") && access(p, F_OK) != 0,
	   "a file without the footer is left alone");

	ck(!dsv_import(dir, "Missing.nds"), "no .dsv, nothing to do");

	/* A temporary left by a power cut mid-write, longer than the save: it is
	 * written over from its start, not kept or added to. */
	snprintf(p, sizeof p, "%s/Cut.dsv", dir);
	put(p, file, sizeof file);
	snprintf(p, sizeof p, "%s/Cut.srm.new", dir);
	put(p, file, sizeof file);
	ck(dsv_import(dir, "Cut.nds"), "a temporary from a cut write is no obstacle");
	snprintf(p, sizeof p, "%s/Cut.srm", dir);
	ck(get(p, back, sizeof back) == 8192 && !memcmp(back, chip, 8192),
	   "and the .srm is the chip's bytes alone, none of the old temporary's");
	snprintf(p, sizeof p, "%s/Cut.srm.new", dir);
	ck(access(p, F_OK) != 0, "the temporary is gone");

	/* A folder that refuses the write: no .srm at all, never an empty one,
	 * so the next run tries again and takes it over. Not as root, which a
	 * folder's mode does not stop. */
	if (geteuid() != 0) {
		snprintf(p, sizeof p, "%s/Later.dsv", dir);
		put(p, file, sizeof file);
		chmod(dir, 0555);
		ck(!dsv_import(dir, "Later.nds"), "a write the folder refuses is a failure");
		chmod(dir, 0755);
		snprintf(p, sizeof p, "%s/Later.srm", dir);
		ck(access(p, F_OK) != 0, "and leaves no .srm behind");
		ck(dsv_import(dir, "Later.nds") && get(p, back, sizeof back) == 8192 &&
		   !memcmp(back, chip, 8192), "so the next run takes the .dsv over");
	}

	{
		static const char *const made[] = {
			"Game (USA).dsv", "Game (USA).srm", "Zipped.dsv", "Zipped.srm", "Raw.dsv",
			"Cut.dsv", "Cut.srm", "Later.dsv", "Later.srm"
		};
		for (i = 0; i < sizeof made / sizeof made[0]; i++) {
			snprintf(p, sizeof p, "%s/%s", dir, made[i]);
			unlink(p);
		}
		rmdir(dir);
	}
	if (fails) { printf("dsv: %d FAILED\n", fails); return 1; }
	printf("ok: every check passed\n");
	return 0;
}
