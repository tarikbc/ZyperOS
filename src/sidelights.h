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
 *   Files and Covers       a wave while files move to or from the card
 *                          (Flash Drive, File Transfer's page), and a climb
 *                          when a File Transfer batch or a Scraper run
 *                          finishes: one row for both (my pick in the
 *                          transfer lights lab, 2026-11-13)
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

/* Files moving: the gauge's wave while files go to or from the card (my
 * picks in the transfer lights lab, 2026-11-13; DESIGN.md 11.27), part of
 * Files and Covers. "move IN OUT", bytes a second coming in to the card and
 * going out of it, as package/gauge/src/lights.h reads it; "move 0 0" when
 * they stop. snprintf's answer. Flash Drive's usbdrive sends the same line
 * (its drivestat.h, dst_lights_line). */
int side_lights_move_line(char *out, size_t cap, unsigned long long in_bps, unsigned long long out_bps);

/* File Transfer's page, a frame at a time: the bytes of files arriving (in)
 * and going (out) since the last frame. A line at most every
 * SIDE_LIGHTS_MOVE_MS while they move, their speed over that time; and
 * "move 0 0" once SIDE_LIGHTS_MOVE_IDLE_MS after the last of them, so a
 * pause between two files of a batch keeps the wave (the gauge holds it
 * 1.5 s past a line). True with the line in `line` when the gauge is to
 * hear one. */
#define SIDE_LIGHTS_MOVE_MS      500u
#define SIDE_LIGHTS_MOVE_IDLE_MS 1000u
typedef struct {
	bool started, on;             /* on: the gauge was told files move */
	unsigned at, moved;           /* the window's start; the last bytes */
	unsigned long long in, out;   /* bytes in the window */
	bool up_was, send_was;        /* side_lights_move_files: the last frame's */
} side_lights_move;
bool side_lights_move_step(side_lights_move *m, unsigned long long in, unsigned long long out,
                           unsigned now_ms, char *line, size_t cap);

/* The same from the server's own counts (hare_stats): the bytes in count
 * only while an upload is on its way, the bytes out only while a download
 * is, so the page itself, its covers and its lists, which a browser loads
 * too, are not a transfer. Or was, the frame before: a file's last bytes
 * land in the poll that ends it. */
bool side_lights_move_files(side_lights_move *m, int uploads, int sends, unsigned long long in,
                            unsigned long long out, unsigned now_ms, char *line, size_t cap);

/* The page closing: "move 0 0" now if the gauge was told files move, and
 * the state as new. True with the line when there is one to say. */
bool side_lights_move_end(side_lights_move *m, char *line, size_t cap);

/* MENU > Side Lights: the row under the cursor played on the lights, On or
 * Off, for as long as the cursor stays on it; the gauge loops it
 * (package/gauge/src/lights.h, "preview bar" to "preview cheer"). Said again
 * every SIDE_LIGHTS_PREVIEW_MS, as the gauge keeps a preview 2.5 s past its
 * last line; another row at once; "preview off" as the page closes. */
#define SIDE_LIGHTS_PREVIEW_MS 1000u
typedef struct { bool on; int part; unsigned at; } side_lights_preview;

/* One pass of the page, with the part under the cursor (not a part: none).
 * True with the line in `line` when the gauge is to hear one. */
bool side_lights_preview_step(side_lights_preview *p, side_light_part part, unsigned now_ms,
                              char *line, size_t cap);

/* The page closing: "preview off" if a preview was said, and the state as
 * new. True with the line when there is one to say. */
bool side_lights_preview_end(side_lights_preview *p, char *line, size_t cap);

#endif
