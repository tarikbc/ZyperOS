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
 * is System Update's (DESIGN.md 11.17, 11.33), drawn by the same function. */
#ifndef ZYPEROS_PMINSTALL_H
#define ZYPEROS_PMINSTALL_H

#include <stdbool.h>
#include <stddef.h>

#include "library.h"
#include "sysupdate.h"

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
} pm_view;

/* The page: System Update's, with this one's words and keys. The keys' acts
 * are SUA_DOWNLOAD (Install, Try Again), SUA_OPEN (Open PortMaster) and
 * SUA_BACK. `notes` gets the points under the line, `*nnotes` how many. */
#define PM_NOTES 3
void pm_page_build(const pm_view *v, su_page *p, char notes[PM_NOTES][120], int *nnotes);

/* Whether a listing of the Ports folder has PortMaster's script */
bool pm_listing_has(const game_list *l);
/* PortMaster's entry added to a listing of the Ports folder that lacks it:
 * the entry a scan would make of Roms/Ports/PortMaster.sh, so the shelf, the
 * saved library and the card read again later all see the same listing
 * whether PortMaster is on the card or not. False when it could not be. */
bool pm_listing_add(game_list *l);

#endif
