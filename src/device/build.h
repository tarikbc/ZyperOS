/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_DEVICE_BUILD_H
#define ZYPEROS_DEVICE_BUILD_H

/* How a handheld is built and laid out (docs/building.md,
 * Devices): its architecture and the
 * build's baseline, the Diatom port it runs, its card's partitions, and the
 * helpers its payload carries beside the launcher. Stated once here, in
 * src/device/<id>_build.c, and read by the build, the card and plastron
 * through the files tools/devicegen.c writes from it, config/device/<id>.mk
 * and <id>.system (make device-files; check-devicegen holds them). The
 * launcher never links this: it is the build's, and keeping it out keeps the
 * launcher's own binary the same. No SDL. */

/* The programs a device's payload carries beside the launcher, MP4 and
 * Diatom (mk/payload.sh), in the order config/device/<id>.mk lists them */
typedef enum {
	DEV_HELPER_PORTKEYS = 1u << 0,   /* tools/portkeys.c: a port's keys and MENU */
	DEV_HELPER_ZYHOME   = 1u << 1,   /* tools/zyhome.c: MENU+START back home */
	DEV_HELPER_UPDATE   = 1u << 2,   /* update/: System Update's program */
	DEV_HELPER_P2D      = 1u << 3,   /* src/p2d/: NES Player 2's server */
	DEV_HELPER_PORTS    = 1u << 4,   /* PortMaster and OpenBOR on the card */
	DEV_HELPER_BTPLAYER = 1u << 5,   /* tools/btplayer.c: a headset's buttons */
	DEV_HELPER_BTALSA   = 1u << 6,   /* sd/zyperos/bt-alsa.sh: Bluetooth sound */
	/* tools/sppad.c: the TrimUI Smart Pro's two serial halves as one
	 * gamepad. In the system, not the card: the image's start script runs
	 * it before ZyperOS (plastron's package/zyperos) */
	DEV_HELPER_SPPAD    = 1u << 7,
} dev_helper;

typedef struct {
	const char *id;          /* the profile's id: "pixel2" */
	const char *arch;        /* "aarch64": the toolchain and the shared core set */
	const char *mcpu;        /* the build's baseline: cortex-a53 for every aarch64 device */
	const char *cpu;         /* the real core, for the record */
	/* src/device/<this>.c (and <this>_hw.c): the platform layer it runs.
	 * The device's own id but where it shares another's board: the Smart
	 * Pro runs the Brick's */
	const char *backend;
	const char *diatom_port; /* diatom/port/<this>.c */
	const char *table;       /* the card's partition table: "mbr" or "gpt" */
	/* The card's partitions by number, 0 for none: the boot partition, the
	 * system's root (and the second one System Update switches to), the
	 * games partition */
	int part_boot, part_root_a, part_root_b, part_games;
	unsigned helpers;        /* dev_helper bits */
} dev_build;

extern const dev_build device_build_pixel2;
extern const dev_build device_build_brick;
extern const dev_build device_build_smartpro;

#endif
