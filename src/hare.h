/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_HARE_H
#define ZYPEROS_HARE_H

#include <stdbool.h>
#include <stddef.h>

#include "httpd.h"

/* JSON out, into a buffer that grows: the replies of the routes here, and the
 * answers hare's hooks give (hareart.c's, and main.c's for album covers).
 * Start from { 0 }, or with .max for a reply that must stay under max bytes.
 * `bad` once a write found no room - `over`, past max - or no memory, and
 * every write after it is dropped. hj_done hands the text over, or frees it
 * and says why. */
typedef struct { char *p; size_t used, cap, max; bool bad, over; } hj;
void  hj_lit(hj *o, const char *s);
void  hj_str(hj *o, const char *s);     /* quoted and escaped */
void  hj_int(hj *o, int v);
char *hj_done(hj *o, size_t *len, char *why, size_t wn);

/* Wi-Fi File Transfer: the routes, the PIN, and the file operations.
 *
 * httpd.c moves bytes and knows nothing about what they mean. xfer.c decides
 * what a path is allowed to be. This is the part in between: what a URL does,
 * who is allowed to do it, and what happens to the card when they do.
 *
 * THE PIN IS FOUR DIGITS, which is ten thousand guesses. On its own that is a
 * doorbell rather than a lock - a script exhausts it in seconds. What makes it
 * a lock is the lockout: a handful of wrong answers and the door stops
 * answering for a while, which turns ten thousand guesses into more hours than
 * anyone will spend on somebody else's ROM folder. The PIN is generated fresh
 * every time the screen opens and dies with it, so there is nothing to reuse
 * and nothing to leak.
 *
 * The threat this defends against is someone else on the same Wi-Fi, which is
 * the realistic one for a handheld on a home or cafe network. It is not
 * defense against someone who can watch the traffic: this is plain HTTP, the
 * PIN crosses the network in the clear, and so does everything else. TLS on a
 * LAN address is a certificate nobody can issue and a warning everybody clicks
 * through, which buys less than it costs. The mitigation that actually applies
 * is that the server exists only while the screen is open.
 */

/* Start and stop with the screen. Generates the PIN, clears any session, and
 * sweeps part-files left by a transfer that died.
 *
 * The three directories are arguments rather than the P_ globals so this whole
 * subsystem - routes, transport, path safety - links into a check without SDL
 * behind it. The launcher passes P_ROMS, P_CARD and its own res/web. */
bool hare_start(const char *roms_dir, const char *card_dir,
                const char *shared_dir, const char *web_dir);
void hare_stop(void);

/* Download logs: how the launcher packs them, which this file cannot do by
 * itself - the masking needs names only the launcher knows (see logpack.h).
 * The hook writes a .tar.gz somewhere temporary and says where, and what the
 * download should be called. Unset, the route answers that there are none. */
void hare_set_logs(bool (*pack)(char *path, size_t pn, char *name, size_t nn));

/* Called with a folder's absolute path just before it is deleted with
 * everything in it, so the launcher can stop Muse if it is playing a file in
 * there. Unset, nothing is told. */
void hare_set_before_delete(void (*fn)(const char *abs));

/* Called with the old and new absolute paths of a music or audiobook folder or
 * file after the page renamed it, so the launcher can move what it keeps under
 * the old name: the cover in .media, Album Art's memory of it, an audiobook's
 * place, and the tracks' paths on the shelf it holds. Tidy names passes the
 * folder, the same path twice when only its files were renamed, and `tidied`,
 * which says the files in it may have been renamed too (by ml_tidy_file).
 * Unset, nothing is told. */
void hare_set_after_rename(void (*fn)(const char *from, const char *to, bool tidied));

/* Box art for the web page, which the launcher answers because it knows the
 * shelves and the matching rule (hareart.c, artscrape.h). The browser fetches
 * the covers itself; these say which games want one and which each gets:
 *
 *   wanted  GET /api/art/wanted, the shelves with games missing covers, as
 *           the JSON the page reads
 *   match   POST /api/art/match?s=<folder>, the body a collection's names,
 *           one a line: that shelf's matches, as JSON
 *   crc     POST /api/art/crc?s=<folder>, the body the collection's No-Intro
 *           list: what matching by name left, matched by checksum
 *   nointro POST /api/art/nointro?s=<folder>, the same list: each missing
 *           zip's No-Intro name by checksum, for the page to ask for directly
 *
 * Each returns malloc'd text the server frees, or NULL with the reason in
 * `why`. Unset, the routes answer 404 and the page hides its button. */
typedef char *(*hare_art_fn)(const char *folder, const char *body,
                             size_t *len, char *why, size_t wn);
void hare_set_art(char *(*wanted)(size_t *len, char *why, size_t wn),
                  hare_art_fn match, hare_art_fn crc, hare_art_fn nointro);

/* Album covers the same way, from MusicBrainz and the Cover Art Archive:
 *
 *   wanted  GET /api/albums/wanted, the albums Album Art would ask about, each
 *           with the MusicBrainz searches to try, in order, as JSON
 *   pick    POST /api/albums/pick?id=N, the body a search's reply: the release
 *           group by Muse's track-count rule and where its cover goes, as
 *           JSON, or an empty "rg" when the reply is not the album
 *
 * Same contract as hare_set_art. */
void hare_set_albums(char *(*wanted)(size_t *len, char *why, size_t wn),
                     char *(*pick)(int id, const char *json, size_t n,
                                   size_t *len, char *why, size_t wn));

/* The SteamGridDB key (src/sgdb.h), which the page can give the device and
 * take off it but never read back: what comes back is its last four
 * characters and what is known about it. A 32-character key is easier pasted
 * from a phone or a computer than typed on the handheld's keyboard.
 *
 *   GET  /api/sgdb          what there is, as JSON
 *   POST /api/sgdb/key      the body the key as pasted: its check starts, and
 *                           the reply is the same JSON, "checking" until the
 *                           page asks again after it is over
 *   POST /api/sgdb/remove   takes it off; the same JSON
 *
 * One hook for the three, `op` saying which, returning malloc'd JSON or NULL
 * with the reason in `why`. The body is a credential: it is wiped once the
 * hook has it, and nothing here logs it or puts it on the screen. Unset, the
 * routes answer 404 and the page hides the key. */
enum { HARE_SGDB_STATE, HARE_SGDB_KEY, HARE_SGDB_REMOVE };
typedef char *(*hare_sgdb_fn)(int op, const char *body, size_t n, size_t *len,
                              char *why, size_t wn);
void hare_set_sgdb(hare_sgdb_fn fn);

/* The hotspot's address while the handheld is its own network (hotspot.h),
 * or NULL. Set, a request for any other host is answered with a redirect to
 * the page: that is a phone's internet check, and the redirect is what opens
 * the page in its sign-in sheet. Set, res/web/captive.js is served (the
 * sheet's note); unset, an empty script. */
void hare_set_captive(const char *addr);

/* Which link the page is served over: the USB cable (true), or Wi-Fi and the
 * handheld's own network (false), as the launcher starts it. The page names
 * itself after it, as the handheld's screen does - USB File Transfer or
 * Wi-Fi File Transfer, in its tab, on the PIN screen and over the folders -
 * from GET /link.js, which needs no session (the PIN screen is named too)
 * and is never cached, so a browser that saw the page over one link is told
 * again over the other. Wi-Fi until set; hare_stop forgets it, so every
 * start says again. */
void hare_set_cable(bool cable);

/* SSH over the same link, as user@address ("root@10.42.0.1"), which the
 * roots' listing names so the page can show the command; NULL or "" is none.
 * USB File Transfer sets it while its SSH listens (sshlink.h). */
void hare_set_ssh(const char *who);

/* A console's color, 0xRRGGBB, for a folder in ROMs or Save States (both
 * hold a folder per system), which the listing gives the page for the tile
 * behind the folder's icon; anything above 0xFFFFFF is none. Unset, no
 * folder has one. */
void hare_set_folder_color(unsigned (*color)(const char *folder));

/* And its icon: the key of the shelf's pixel art for a folder in ROMs or
 * Save States ("NES", "SNES", "BGM"), or NULL for none, which the listing
 * names as "icon". The pictures themselves come from hare_set_icons. Unset,
 * no folder has one, and the page draws its plain folder. */
void hare_set_folder_icon(const char *(*icon)(const char *folder));

/* Where those pictures are: <dir>/<KEY>-still.png, the shelf's own
 * (res/system-icons), served to a signed-in browser as /icons/<KEY>.png.
 * NULL or unset serves none. */
void hare_set_icons(const char *dir);

/* The card's free and whole size in bytes, in place of statvfs's: for
 * tools/portal-serve.c's pictures, which run on a computer's disk rather
 * than a card. NULL, as on the device, asks the card. */
void hare_set_space(bool (*fn)(long long *free, long long *total));

/* Called with a file's absolute path once an upload of it is in place, so the
 * launcher can shrink a cover. Unset, nothing is told. */
void hare_set_after_write(void (*fn)(const char *abs));

/* The most a delete takes at once, files and folders together. More than
 * this is almost certainly the wrong folder, and it is asked for in parts. */
#define HARE_DELETE_MAX 1000

/* What the card keeps free whatever the page sends: a save, a save state and
 * the databases still find room on a card a transfer filled, and an upload
 * that would go past it is refused (2026-11-06: "are we checking if
 * the files we are sending can actually fit"). The page adds a batch up
 * against GET /api/state first; this is the last word, per file. */
#define HARE_RESERVE (100LL * 1000 * 1000)

/* Stopping while files move (DESIGN.md 11.14, the stop question): after
 * hare_close_after, no upload or download starts - the page hears it from
 * GET /api/state ("closing") and from the 503 a new one gets - and the ones
 * on their way go on. The screen closes the server once hare_inflight is 0.
 * hare_stop is the other answer, at once: what is half written is a .part
 * file, deleted with its connection, never a broken game on the shelf. */
void hare_close_after(void);
bool hare_closing(void);
int  hare_inflight(void);          /* uploads and downloads on their way */

/* Milliseconds since a signed-in browser last asked anything (a request
 * without a session, a PIN tried included, does not count): a File Transfer
 * hidden behind the shelf closes by itself after long enough (main.c). */
unsigned hare_quiet_ms(void);

/* System Update over the USB cable (docs/updates.md): the page brings a
 * release's files from the computer's internet, and the launcher checks them
 * and installs them as System Update does. The computer only carries them.
 *
 *   GET  /api/update              what the handheld has and is doing (JSON)
 *   PUT  /api/update/file?name=N  one of the release's files
 *   POST /api/update/check        the files checked: signature, device, version
 *   POST /api/update/install      staged, as System Update's Download
 *   POST /api/update/restart      the handheld restarts to install it
 *
 * One hook, `op` saying which. HARE_UPDATE_FILE is asked before the body,
 * with the file's `name`: it answers where the body goes in `path` (the
 * server writes <path>.part and renames it), or NULL with the reason in
 * `why` to refuse the name; HARE_UPDATE_FILED after it arrived whole. Each
 * returns malloc'd JSON the server frees, or NULL with the reason. Unset,
 * the routes answer 404 and the page says updates are not on this page. */
enum {
	HARE_UPDATE_STATE, HARE_UPDATE_FILE, HARE_UPDATE_FILED,
	HARE_UPDATE_CHECK, HARE_UPDATE_INSTALL, HARE_UPDATE_RESTART
};
typedef char *(*hare_update_fn)(int op, const char *name, char *path, size_t pn,
                                size_t *len, char *why, size_t wn);
void hare_set_update(hare_update_fn fn);

/* What the screen puts on the panel. `ip` is the LAN address, from wifi. */
const char *hare_pin(void);
/* Every wrong PIN since hare_start, the 30-second waits notwithstanding:
 * USB File Transfer's SSH counts them toward its own five (sshlink.h). */
int         hare_wrong_pins(void);
int         hare_port(void);

/* Drive it from the screen's frame loop. Returns non-zero when anything moved,
 * which is what keeps Auto Off from powering the device off mid-upload. */
int hare_poll(void);

/* For the screen: what is going on, in words a person can read. */
typedef struct {
	int           clients;      /* browsers currently connected */
	unsigned long in, out;      /* bytes since the last call */
	int           uploads;      /* transfers in flight */
	int           sends;        /* downloads in flight */
	char          last[128];    /* the most recent thing that happened */
	char          file[128];    /* the upload on its way, or the last one */
	long long     up_got, up_total;  /* the uploads on their way: bytes in, and of */
	unsigned long requests;     /* every request since hare_start: one is a browser */
} hare_stats;

void hare_status(hare_stats *out);

/* Whether anything under the ROM, Music or Audiobooks root was written,
 * renamed or deleted since the screen opened. The shelf is scanned once at
 * startup and nothing watches the card, so a ROM, an album or a book arriving
 * over the network is
 * invisible until something rescans - which is what the screen uses this to
 * decide on the way out. */
bool hare_shelf_changed(void);
/* ...and answered: the launcher read the shelf again, so the next change is
 * a new one. For a File Transfer that runs behind the shelf, which rescans
 * when the files stop rather than when the screen closes. */
void hare_shelf_seen(void);

#endif
