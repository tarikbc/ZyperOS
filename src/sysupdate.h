/* SPDX-License-Identifier: GPL-3.0-only */
/* MENU > System Update: what its page says, from what the update program
 * (update/, docs/updates.md) last wrote and what the device knows. Like
 * sys_menu.c, nothing here touches SDL, a device or a file: main.c reads the
 * status file and the folders, and asks this for the page, so `make check`
 * can hold every state's words (tools/sysupdate-check.c). */
#ifndef ZYPEROS_SYSUPDATE_H
#define ZYPEROS_SYSUPDATE_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
	SU_IDLE, SU_UNSUPPORTED, SU_CHECKING, SU_UPTODATE, SU_AVAILABLE,
	SU_NEEDS_SYSTEM, SU_COPYING, SU_DOWNLOADING, SU_VERIFYING, SU_INSTALLING, SU_READY,
	SU_STOPPED, SU_ERROR, SU_BATTERY, SU_SPACE, SU_ROLLBACK,
	SU_TOO_BIG               /* a system bigger than this card's slots */
} su_state;

/* The status file, as update/status.c writes it */
typedef struct {
	su_state state;
	char installed[32], version[32];
	unsigned long long download, done, total;
	unsigned long long size;  /* the new folder, every file */
	int files_done, files_total;
	char message[160];
	bool system;              /* a system update (kind system): the root and
	                           * kernel for the other slot, docs/updates.md */
	char other[32];           /* the version in the other system slot */
	long long time;           /* when it was written, unix seconds */
} su_status;

/* The text of the status file; a missing or empty one is SU_IDLE. */
void su_status_parse(const char *text, su_status *out);

/* Everything the screen shows */
typedef struct {
	su_status st;
	bool running;             /* the program is at work now */
	bool wifi;                /* a network to look with */
	char installed[32];       /* ZyperOS/VERSION */
	char prev[32];            /* the version Roll Back goes to; "" for none */
	char result[80];          /* update/result: "updated A B", "failed V", "rolled-back V" */
	bool notes;               /* the release has a What's New */
	int battery;              /* percent */
	bool charging;
	char checked[16];         /* when it last looked, "9:41"; "" for never */
} su_view;

/* What a button does on the page. SUA_OPEN is the Install PortMaster page's
 * (src/pminstall.h), which is this page with its own words: open what was
 * installed. */
typedef enum { SUA_NONE, SUA_BACK, SUA_CHECK, SUA_DOWNLOAD, SUA_RESTART, SUA_ROLLBACK,
               SUA_OPEN } su_action;

#define SU_MIN_BATTERY 25
#define SU_MAX_KEYS 3

typedef struct {
	char button[8];          /* "A", "B", "Y" */
	char label[48];          /* room for a language longer than English */
	su_action act;
} su_key;

/* The full page (the update lab's pick, 2026-11-05; DESIGN.md 11.17): the
 * versions in big type, a line under them, What's New, one bar or one line
 * where the bar goes, and the buttons at the foot. */
typedef struct {
	char from[32], to[32];   /* to "": only the installed version */
	char sub[256];
	bool sub_warn;           /* the line under the versions in **warning** */
	bool notes;              /* What's New under it */
	bool bar;
	float frac;
	char bar_left[32], bar_right[48];
	char line[256];          /* where the bar goes, when there is none */
	bool line_warn;
	/* A problem, said in full where What's New goes (which steps aside):
	 * big enough to read at arm's length, not a cut line in **meta** */
	char warn[160];
	su_key keys[SU_MAX_KEYS];
	int nkeys;
} su_page;

void su_page_build(const su_view *v, su_page *p);
/* The value of MENU's System Update row: the installed version, or the news. */
const char *su_menu_value(const su_view *v, char *buf, size_t n);
/* 0.10.0 > 0.9.2; 0.2.0-beta.1 < 0.2.0, as the update program compares */
int su_version_cmp(const char *a, const char *b);
/* 14 MB, 6.1 MB, 950 KB: one decimal under 10. */
void su_size(unsigned long long bytes, char *buf, size_t n);
#endif
