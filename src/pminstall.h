/* SPDX-License-Identifier: GPL-3.0-only */
/* Install PortMaster: the Ports shelf's page for putting the official
 * PortMaster (portmaster.games) on the card when it is not there.
 *
 * The Ports shelf has a PortMaster entry whether or not PortMaster is on the
 * card: the same file name the real one has, Roms/Ports/PortMaster.sh, so a
 * favorite, its play time and its art carry over once it is installed. A on
 * it, while PortMaster is not on the card, opens this page instead of the
 * port. The page's Install starts res/portmaster/pm-install.py, which
 * downloads PortMaster's latest stable release from GitHub, checks it and
 * unpacks it into Roms/Ports, writing what it is doing to a status file; the
 * page draws that file. Nothing here touches SDL, a device or a file:
 * src/main.c reads the status and the card and asks this for the page, so
 * `make check` holds every state's words (tools/pminstall-check.c). The page
 * is System Update's (DESIGN.md 11.17, 11.33), drawn by the same function.
 *
 * The same page reinstalls a PortMaster that is on the card (pm-install.py
 * --reinstall): a fresh copy of the newest stable release, the ports beside
 * it and their saves untouched, its runtimes, themes and waiting zips kept,
 * its settings started over. It opens from Reinstall PortMaster on
 * PortMaster's details page, and by itself when PortMaster closed with an
 * error (pm_failure): PortMaster's own self-update can leave it unable to
 * start, and before this a player had nothing to do but watch it fall back
 * to the shelf. */
#ifndef ZYPEROS_PMINSTALL_H
#define ZYPEROS_PMINSTALL_H

#include <stdbool.h>
#include <stddef.h>

#include "library.h"
#include "sysupdate.h"

/* The longest line pm_failure keeps of PortMaster's log */
#define PM_WHY 96

/* The script the Ports shelf runs, and the folder beside it, as PortMaster's
 * own installer lays them out on a system it has no layout for */
#define PM_SCRIPT "PortMaster.sh"
#define PM_FOLDER "PortMaster"
/* What PortMaster's folder has when it is whole: its program */
#define PM_PROGRAM "pugwash"
/* The installer, in the card's ZyperOS folder (mk/payload.sh, the Pixel 2's
 * only: only its launch.sh runs ports), and where it says what it is doing */
#define PM_INSTALLER "portmaster/pm-install.py"
#define PM_STATUS_DIR "/tmp/zyperos-pm"
/* The entry's own art, beside the installer (tools/genportmaster.py): its
 * cover in each theme, and the icon the Install PortMaster page draws */
#define PM_COVER_LIGHT "portmaster/cover-light.png"
#define PM_COVER_DARK  "portmaster/cover-dark.png"
#define PM_ICON        "portmaster/icon.png"

typedef enum {
	PM_IDLE,                  /* nothing started: the offer */
	PM_CHECKING, PM_DOWNLOADING, PM_VERIFYING, PM_UNPACKING,
	PM_DONE, PM_ERROR
} pm_state;

typedef enum {
	PM_ERR_NONE, PM_ERR_NETWORK, PM_ERR_DOWNLOAD, PM_ERR_CHECKSUM, PM_ERR_SPACE,
	PM_ERR_UNPACK, PM_ERR_RELEASE, PM_ERR_INTERNAL
} pm_error;

/* The status file, as pm-install.py writes it */
typedef struct {
	pm_state state;
	pm_error error;
	bool reinstall;           /* kind reinstall: a fresh copy over the one there */
	char version[32];
	unsigned long long done, total, need;
	long long time;
} pm_status;

/* The text of the status file; a missing or empty one is PM_IDLE. */
void pm_status_parse(const char *text, pm_status *out);

/* Everything the page shows */
typedef struct {
	pm_status st;
	bool running;             /* the installer is at work now */
	bool network;             /* a way out: Wi-Fi, or a route on another link */
	bool python;              /* the system has Python 3 (a system from before
	                           * 1.0.2 has none, and PortMaster runs on it) */
	bool installed;           /* PortMaster is on the card, whole */
	/* The page opened on a PortMaster that is on the card, to reinstall it:
	 * the offer until A, the run's own word from then on (`asked`), or at
	 * once when a reinstall runs already */
	bool repair;
	bool asked;
	char have[32];            /* PortMaster/version on the card, "" unknown */
	char failed[PM_WHY];      /* how it closed with an error, "" for no word */
	bool crashed;             /* it closed with an error: failed[] says how */
} pm_view;

/* The page: System Update's, with this one's words and keys. The keys' acts
 * are SUA_DOWNLOAD (Install, Try Again), SUA_OPEN (Open PortMaster) and
 * SUA_BACK. `notes` gets the points under the line, `*nnotes` how many. */
#define PM_NOTES 3
void pm_page_build(const pm_view *v, su_page *p, char notes[PM_NOTES][120], int *nnotes);

/* PortMaster's own log, beside its program, which its launcher script writes
 * whole each start (everything the start printed, its program's errors
 * included); and the record launch.sh leaves of each PortMaster run for the
 * launcher that comes back after it: "START END STATUS", seconds */
#define PM_LOG "log.txt"
#define PM_RAN "/tmp/zyperos_pm_ran"

/* Whether the tail of PortMaster's log shows that it closed with an error:
 * its program killed by a signal (bash's "Segmentation fault ... ./pugwash"),
 * Python's "Fatal Python error", an exception nothing caught (Python's own
 * traceback, or loguru's "An error has been caught", which is PortMaster's
 * main() failing) at the end of the log, or SDL finding no display. A
 * traceback PortMaster logged and went on from, with its window still up
 * after it, is not one: an error is an error only with no more than a few
 * lines after it. `why`, n bytes, gets the line that says how (the
 * exception, "ModuleNotFoundError: No module named 'x'"), cut to fit. */
bool pm_failure(const char *log, char *why, size_t n);

/* PortMaster's settings and cached lists, config/ beside its program, are
 * JSON it writes whole each start and after each install, with no sync: a
 * write cut off (the handheld off, PortMaster ended mid-write) leaves a file
 * that is empty, cut short or zeros. The stable release, 2026.06.23-0015,
 * then never starts again: a cut-off runtimes.json fails its list_runtimes
 * with "'NoneType' object has no attribute 'items'" as its window says
 * "Loading Info." (fixed in its 2026.11 betas), and a cut-off config.json
 * fails before it. The launcher removes such a file before PortMaster
 * starts, and PortMaster makes it again (pm_heal_config, src/main.c).
 *
 * Whether a file whose first bytes are `head` (hn of them) and last `tail`
 * (tn) is a whole JSON object or array: a { or [ first, past a byte-order
 * mark and white space, and the } or ] that closes it last. Empty, cut
 * short and zero-filled files are not. */
bool pm_json_whole(const char *head, size_t hn, const char *tail, size_t tn);
/* The same of a whole file, `n` bytes: every { and [ closed, outside its
 * strings, with nothing but white space after the last, and no zero byte.
 * For the small ones (PM_JSON_SCAN): runtimes.json is 40 KB, its sources
 * list 4 MB, which only its ends are read of. */
bool pm_json_balanced(const char *buf, size_t n);
#define PM_JSON_SCAN (256 * 1024)
/* What else goes with a cut-off file: the settings, config.json, with a
 * cut-off source list (NNN_name.source.json), which PortMaster writes again
 * only on the start it takes for its first, with no config.json */
bool pm_json_resets_settings(const char *name);
#define PM_CONFIG "config"
#define PM_SETTINGS "config.json"

/* Whether a listing of the Ports folder has PortMaster's script */
bool pm_listing_has(const game_list *l);
/* PortMaster's entry added to a listing of the Ports folder that lacks it:
 * the entry a scan would make of Roms/Ports/PortMaster.sh, so the shelf, the
 * saved library and the card read again later all see the same listing
 * whether PortMaster is on the card or not. False when it could not be. */
bool pm_listing_add(game_list *l);

#endif
