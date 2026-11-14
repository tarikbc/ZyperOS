/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_RAHASH_H
#define ZYPEROS_RAHASH_H

#include <stdbool.h>
#include <stddef.h>

/* RetroAchievements identifies a game by an MD5 over the ROM - but not always
 * over the WHOLE ROM. Each console has its own rule about what to skip, and
 * getting one wrong produces a hash RA has never seen, which is indistinguish-
 * able from a game it does not know.
 *
 * Two couplings worth stating, because both fail silently:
 *
 *   - THE ENTRY. A zipped archive is hashed on its LARGEST entry, which is the
 *     one Diatom loads (its src/zip.c). Taking the first instead would hash one
 *     game and run another, and every achievement would be for something else.
 *   - THE RULES. They are RetroAchievements', restated here in C and in
 *     tools/ra-check.py. Both are checked against the same ROMs; see
 *     `make check-rahash`.
 */

/* Writes 32 lowercase hex digits plus a NUL, so `out` needs 33 bytes.
 * `tag` is a systems.cfg tag - NES, SFC, MD, GB. False means the file could
 * not be read or the archive could not be opened. */
bool ra_hash_rom(const char *path, const char *tag, char *out);

/* Whether ra_hash_rom could identify a game at all, decided from the file's
 * name alone: nothing is read, so the Achievements rows can ask it on every
 * open, with or without a network.
 *
 *   RA_HASH_OK         it may: the hash it makes may still be one
 *                      RetroAchievements does not know, which only asking
 *                      tells
 *   RA_HASH_NO_FILE    this kind of file is refused unread (a .chd on the
 *                      PlayStation), though another the shelf takes is not
 *   RA_HASH_NO_SYSTEM  every kind of file the shelf takes is (`exts`,
 *                      systems.cfg's list): the PSP, whose discs have no walk
 *                      here, nor any other disc-only system's
 *
 * Not OK is final, so a row says so rather than "not checked yet", which
 * promises a play with Wi-Fi that would ask nothing. ra_hash_rom refuses by
 * the same test before it opens the file, so the two cannot disagree. */
typedef enum {
	RA_HASH_OK,
	RA_HASH_NO_FILE,
	RA_HASH_NO_SYSTEM,
} ra_hash_kind;

ra_hash_kind ra_hash_support(const char *tag, const char *path, const char *exts);

/* Plain MD5 of a buffer, same 33-byte output. Exposed because submitting an
 * unlock needs one too: RetroAchievements signs the request with
 * md5(achievement id + username + hardcore flag) and checks it server-side.
 * One implementation, two callers - a second MD5 in this repository would be
 * a second thing to get subtly wrong. */
void ra_md5_hex(const void *data, size_t len, char *out);

#endif
