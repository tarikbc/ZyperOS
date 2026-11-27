/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_RAPACK_H
#define ZYPEROS_RAPACK_H

/* A set's achievement badges in one file of the card's, a pack:
 *
 *     Roms/<System>/.cheevos/badges/<set's name>.pack
 *
 * where they were one PNG each (src/rabadge.h), about a hundred thousand on
 * a card with every set's, and fsck.exfat took minutes over them at a start
 * after a power cut (2026-11-24, on the Discord). A pack is one file a set.
 *
 *     "ZYBP"  u32 version (1)  u32 count
 *     count x { char name[RP_NAME] (NUL-padded), u32 offset, u32 size }
 *     the PNGs, one after another
 *
 * little-endian, offsets from the file's start. A fetch still lands loose
 * PNGs (net_get_pngs, one curl a set), and rp_pack takes them in at once;
 * a card's old loose badges are taken in by the same call, set by set
 * (rb_migrate at each start, and whenever a set's badges are asked for). A
 * loose file is deleted only once the pack that holds it is written,
 * synced, read back whole and found the same, and put in place by a
 * rename, so a power cut at any moment leaves each badge in the old pack,
 * the new one or its own file. Pure C, no SDL: tools/rapack-check.c. */

#include <stdbool.h>
#include <stddef.h>

#define RP_NAME   24      /* a badge's name, as radl.h's sets carry it */
#define RP_MAX    1024    /* the most a pack holds */
#define RP_MAGIC  "ZYBP"

/* The pack of the set at `set_path`: <its folder>/badges/<its name less
 * .set>.pack. False for no path, or one too long. */
bool rp_pack_path(const char *set_path, char *out, size_t n);

/* Badge `name` in pack `pack`: its bytes, malloc'd (the caller frees), and
 * their length; NULL when the pack has no such badge or cannot be read */
void *rp_read(const char *pack, const char *name, size_t *len);
/* The pack has it */
bool rp_has(const char *pack, const char *name);

/* Each loose badge of `names` the set's folder has (badges/<name>.png) into
 * the set's pack, beside what the pack holds already, a loose one in place
 * of a packed one of the same name; then those loose files deleted, once
 * the pack is safe (above). A loose file that is no PNG (a link's text, a
 * cut fetch) is neither packed nor deleted. The badges the pack holds after,
 * or -1 when it could not be written (nothing deleted); `took` (NULL for
 * none) the loose ones it took in. Nothing loose, the pack untouched. */
int rp_pack(const char *set_path, const char (*names)[RP_NAME], int n, int *took);

#endif
