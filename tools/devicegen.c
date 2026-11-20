/* SPDX-License-Identifier: GPL-3.0-only */
/* The build's own copy of each device's facts (src/device/build.h), for the
 * places that are not C: config/device/<id>.mk for make (mk/cross.mk,
 * mk/payload.sh, plastron's packages) and config/device/<id>.system, the
 * lines plastron adds to the image's /etc/zyperos/system. Written from
 * src/device/<id>_build.c, so a value is stated once:
 *
 *   devicegen DIR     writes DIR/<id>.mk and DIR/<id>.system for each device
 *
 * make device-files writes them into config/device/, where they are kept in
 * git, and make check-devicegen writes them again and compares. */
#include <stdio.h>
#include <string.h>

#include "../src/device/build.h"

static const dev_build *const BUILDS[] = { &device_build_pixel2, &device_build_brick, &device_build_smartpro };

static const struct { unsigned bit; const char *word; } HELPERS[] = {
	{ DEV_HELPER_PORTKEYS, "portkeys" }, { DEV_HELPER_ZYHOME, "zyhome" },
	{ DEV_HELPER_UPDATE, "update" },     { DEV_HELPER_P2D, "p2d" },
	{ DEV_HELPER_PORTS, "ports" },       { DEV_HELPER_BTPLAYER, "btplayer" },
	{ DEV_HELPER_BTALSA, "btalsa" },     { DEV_HELPER_SPPAD, "sppad" },
};

static int write_mk(const char *dir, const dev_build *b)
{
	char path[512];
	FILE *f;
	size_t i;

	snprintf(path, sizeof path, "%s/%s.mk", dir, b->id);
	if (!(f = fopen(path, "w"))) { perror(path); return 1; }
	fprintf(f, "# %s's build: written by tools/devicegen.c from src/device/%s_build.c\n"
	           "# (make device-files). Do not edit; change that file and write it again.\n",
	        b->id, b->id);
	fprintf(f, "ZYDEV_ID = %s\nZYDEV_ARCH = %s\nZYDEV_MCPU = %s\nZYDEV_CPU = %s\nZYDEV_BACKEND = %s\n"
	           "ZYDEV_DIATOM_PORT = %s\n",
	        b->id, b->arch, b->mcpu, b->cpu, b->backend, b->diatom_port);
	fprintf(f, "ZYDEV_HELPERS =");
	for (i = 0; i < sizeof HELPERS / sizeof HELPERS[0]; i++)
		if (b->helpers & HELPERS[i].bit) fprintf(f, " %s", HELPERS[i].word);
	fprintf(f, "\n");
	return fclose(f) != 0;
}

static int write_system(const char *dir, const dev_build *b)
{
	char path[512];
	FILE *f;

	snprintf(path, sizeof path, "%s/%s.system", dir, b->id);
	if (!(f = fopen(path, "w"))) { perror(path); return 1; }
	fprintf(f, "table %s\n", b->table);
	if (b->part_boot) fprintf(f, "part-boot %d\n", b->part_boot);
	if (b->part_root_a) fprintf(f, "part-root-a %d\n", b->part_root_a);
	if (b->part_root_b) fprintf(f, "part-root-b %d\n", b->part_root_b);
	if (b->part_games) fprintf(f, "part-games %d\n", b->part_games);
	return fclose(f) != 0;
}

int main(int argc, char **argv)
{
	size_t i;
	int bad = 0;

	if (argc != 2) {
		fprintf(stderr, "usage: devicegen DIR\n");
		return 2;
	}
	for (i = 0; i < sizeof BUILDS / sizeof BUILDS[0]; i++)
		bad += write_mk(argv[1], BUILDS[i]) + write_system(argv[1], BUILDS[i]);
	return bad ? 1 : 0;
}
