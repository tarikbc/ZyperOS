/* SPDX-License-Identifier: GPL-3.0-only */
/* Developer mode on a release image (2026-11-07).
 *
 * A release image starts no SSH at boot and carries no key (plastron's
 * post-build takes both out), so a player cannot get a shell. Developer mode
 * is how a player who wants one gets it, on purpose and on their own terms:
 *
 *  - Unlock: on About's first page, hold L2 and R2 together for five
 *    seconds. A bar under the rows fills as the seconds pass, and letting go
 *    of either one first starts again, so a player who never holds both for
 *    that long never sees it, and a bump of one shoulder does nothing.
 *    (L2 and R2 together are also the battery level, which they show once as
 *    they go down: harmless.)
 *  - On: the setting is kept in the device database (db_dev, "devmode"), so
 *    it follows the card's own settings and a new card starts without it.
 *    About says "Developer mode: On"; MENU's Developer Mode row opens its
 *    page, where A turns it off, and the next unlock is the hold again.
 *  - What it adds: what a development image has, on the player's terms. The
 *    USB link stays up while a computer is on the cable and nothing is asked
 *    (usblink_release_dev), and SSH runs on the cable's address alone
 *    (src/sshlink.h) for as long as the cable is in: it is started once per
 *    plug from every screen's loop and a game's (dev_ssh_tick in main.c), so
 *    closing a page, starting a game or restarting the launcher (a deploy)
 *    does not end the way in; a restart ends the sessions and brings SSH back
 *    at once, so a key logs in again seconds later. Two ways in:
 *      * a key the player put on the card, in its Developer folder (a .pub
 *        file, or authorized_keys) (devmode_import_dir). The card is the
 *        player's own, a key there is theirs to put, and nothing is shipped
 *        in the image. Keys that ssh-copy-id saves under .userdata stay;
 *      * a password made for each plug from the system's random numbers
 *        (devmode_password), kept in memory only and shown only on MENU >
 *        Developer Mode's page. It is how the first key gets in without a
 *        card reader: sign in with it, then ssh-copy-id.
 *    Neither is fixed, and the same five-wrong-guesses lockout as File
 *    Transfer's applies to the password (until the next plug).
 *
 * A development image (one that starts dropbear at boot) is a developer's
 * already: none of this changes what it does, and its Developer (SSH) stays
 * the plain link (its dropbear and its key are plastron's, local/).
 *
 * Everything here but the database is plain and has no device in it, so
 * tools/devmode-check.c holds it on the host. */
#ifndef DEVMODE_H
#define DEVMODE_H

#include <stdbool.h>
#include <stddef.h>

#include "db.h"

/* ---- the unlock ------------------------------------------------------- */

#define DEVMODE_HOLD_MS 5000u      /* L2 and R2 held together, for this long */

typedef struct {
	bool     held;                 /* both were down at the last look */
	bool     fired;                /* it opened, and they have not both come up since */
	unsigned since;                /* when both went down */
} devmode_hold;

/* One look per frame, with whether L2 and R2 are both down now. True exactly
 * once per hold: the frame the five seconds are up. Letting go of either one
 * starts again, and after it has opened both must come up before it can
 * again. */
bool devmode_hold_step(devmode_hold *h, bool both_down, unsigned now_ms);

/* How far along the hold is, 0 to 1000: 0 when nothing is held (or it has
 * opened and is being held still), 1000 at the end. For the bar. */
int devmode_hold_permille(const devmode_hold *h, unsigned now_ms);

/* Another page, or About closing: back to the start. */
void devmode_hold_reset(devmode_hold *h);

/* ---- the setting ------------------------------------------------------ */

/* Unlocked on this card. A development image has Developer (SSH) without it
 * (usblink_dev_image), which the caller joins with this. */
bool devmode_unlocked(db *dev);
void devmode_set(db *dev, bool on);

/* ---- what Developer (SSH) does on a release image ---------------------- */

#define DEVMODE_PASSWORD_LEN 8

/* Where the player puts a key (a .pub file, or authorized_keys): a folder at the card's root, not inside
 * ZyperOS/, whose whole folder an update swaps for the new version's (and
 * the old one is gone with the update after) */
#define DEVMODE_KEYS_DIR  "Developer"
#define DEVMODE_KEYS_FILE "authorized_keys"

/* A password for one opening of the screen: DEVMODE_PASSWORD_LEN characters
 * from letters and digits that are not mistaken for each other, drawn from
 * the system's random numbers with no bias. False, and an empty string, if
 * the system has none: then the screen does not start SSH at all. `n` is at least
 * DEVMODE_PASSWORD_LEN + 1. */
bool devmode_password(char *out, size_t n);

/* The player's keys: the public keys in the file `src` (one of the Developer
 * folder's) added to `keydir`/authorized_keys, which
 * File Transfer's ssh-copy-id also writes. Only a line that is a public key
 * counts - a key type, the key, an optional comment - and never one with
 * options in front (a forced command or a from= list would be the file's
 * author deciding what the card's owner can do, and a card is passed
 * around). Lines already there are not added twice. Returns the number of
 * keys now in `src` that were taken (new or already there), -1 if `src` is
 * not there or `keydir` could not be written, and 0 for a file with no key
 * in it. */
int devmode_import_keys(const char *src, const char *keydir);

/* A folder of them: every file in `dir` named authorized_keys or ending in
 * .pub (what ssh-keygen writes, so a player can copy id_ed25519.pub straight
 * in), at most DEVMODE_KEY_FILES of them, each through devmode_import_keys.
 * Returns the number of keys taken, or -1 if the folder is not there. */
#define DEVMODE_KEY_FILES 16
int devmode_import_dir(const char *dir, const char *keydir);

/* A line's verdict, for the check: a public key line, or not. */
bool devmode_key_line(const char *line);

#endif
