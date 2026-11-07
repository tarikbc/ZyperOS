/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_SIDELIGHTS_H
#define ZYPEROS_SIDELIGHTS_H

#include <stdbool.h>
#include <stddef.h>

/* MENU > Side Lights, on a device that shows the battery on lights of its own
 * (plat_battery_lights: the GKD Pixel 2's four, which plastron's gauge
 * drives). A page of five switches, each part of what they show on its own
 * (My picks in the side lights lab, 2026-11-06; DESIGN.md 11.27):
 *
 *   Charging Bar           a bar filling on the cable, still when full and
 *                          at the charge limit
 *   Low Battery            the red flash: under 10% once every 5 s, under 5%
 *                          twice every 2 s
 *   Volume and Brightness  the level on the lights while it changes, eight
 *                          steps, a blinking light for a half
 *   Files and Covers Done  a climb when a File Transfer batch or a Scraper
 *                          run finishes
 *   Achievements           a sparkle when one unlocks
 *
 * Every part on for a new card and for one that said Auto. L2+R2 flashes the
 * level in all four in every setting: it is a press made on purpose (
 * 2026-11-05). The lights are on or off, nothing between (GPIO pins, no PWM),
 * so there is no brightness to offer.
 *
 * The setting is a line in a file the gauge reads (plat_side_lights), not a
 * row in the database: the gauge starts long before the launcher and reads it
 * at boot, and one place to keep it means nothing to keep in step. The line
 * starts with one of the four words the gauge knew before the parts (auto,
 * charging, low, off: the bar and the red flash), then a word for each newer
 * part, "level" for on and "-level" for off; a newer part the line does not
 * name is on after auto and off after the others. So an older gauge still
 * reads the battery's parts right, and a card set to Charging, Low Battery
 * or Off before keeps what it said. plastron's gauge reads it the same way
 * (package/gauge/src/lights.c, lights_parse): a change here is a change there.
 *
 * Pure but for side_lights_say_to's one write, so tools/menu-check.c and
 * tools/sidelights-check.c hold it all without a device. */
typedef enum {
	LP_BAR,
	LP_LOW,
	LP_LEVEL,
	LP_DONE,
	LP_CHEER,
	LP_COUNT
} side_light_part;

/* The parts that are on, a bit each (1u << LP_BAR ...) */
typedef unsigned side_lights;
#define LIGHTS_ALL ((1u << LP_COUNT) - 1)

/* The setting's line, and back. NULL, empty or unknown first words read as
 * auto, which is every part. */
side_lights side_lights_parse(const char *text);
void side_lights_line(side_lights on, char *out, size_t cap);

/* A part's row on the page, and the page's row in MENU: "All on", "3 on",
 * "Off" */
const char *side_lights_part_name(side_light_part p);
const char *side_lights_value(side_lights on, char *buf, size_t cap);

static inline bool side_lights_has(side_lights on, side_light_part p) { return (on >> p) & 1u; }
static inline side_lights side_lights_flip(side_lights on, side_light_part p) { return on ^ (1u << p); }

/* ---- what the launcher tells the gauge --------------------------------------
 *
 * Beside the setting, the launcher sends plastron's gauge one line for each
 * thing worth a glance (package/gauge/src/lights.h, lights_line), on its FIFO.
 * The gauge decides what shows, by the setting and what else is lit; the
 * launcher only says what happened, and never waits to be heard. */
#define SIDE_LIGHTS_FIFO "/run/gauge"

#define SIDE_LIGHTS_DONE     "done"     /* a File Transfer batch or a Scraper run finished */
#define SIDE_LIGHTS_CHEER    "cheer"    /* an achievement unlocked */

/* "level volume 12 20" or "level brightness 3 11": the level as it changes,
 * val of max. snprintf's answer. */
int side_lights_level_line(char *out, size_t cap, bool volume, int val, int max);

/* One line into `fifo`, never waiting: false when no gauge reads it (ENXIO:
 * none running, or one too old for the FIFO), when its buffer is full, or
 * when anything else goes wrong, and nothing is kept for later - these are
 * glances. A gauge that dies between the open and the write raises SIGPIPE,
 * which is caught here rather than ending the caller. */
bool side_lights_say_to(const char *fifo, const char *line);

/* File Transfer's batches, for SIDE_LIGHTS_DONE: true once, when files were
 * arriving and none has for SIDE_LIGHTS_BATCH_MS. The page sends a batch one
 * file after another with a moment between them, so the end of one file is
 * not yet the end of the batch. */
#define SIDE_LIGHTS_BATCH_MS 2000u
typedef struct { bool moved; unsigned last; } side_lights_batch;
bool side_lights_batch_done(side_lights_batch *b, bool arriving, unsigned now_ms);

#endif
