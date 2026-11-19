/* SPDX-License-Identifier: GPL-3.0-only */
/* MENU > Downloads: src/dl_screen.h */
#include "dl_screen.h"

#include "db.h"
#include "dlq.h"
#include "menu_pick.h"
#include "net.h"
#include "radl_screen.h"
#include "rafetch.h"
#include "scraper_screen.h"
#include "sgdb.h"
#include "signin_screen.h"
#include "ss.h"
#include "sys_menu.h"

#include <stdio.h>
#include <string.h>

/* The page's rows, in order: Download, then the kinds, then Settings */
enum { DL_DOWNLOAD, DL_RULE1, DL_COVERS, DL_VIDEOS, DL_NAMES, DL_ACH, DL_RULE2, DL_SETTINGS, DL_ROWS };

typedef struct {
	app *a;
	scraper_counts sc;
	radl_counts rc;
	bool counted;
	unsigned ends[DLQ_LANES];   /* each lane's ends when the page last counted */
	char val[DL_ROWS][64];
	menu_result said;
} dl_ctx;

bool downloads_kind_in(const char *kind)
{
	char key[48], v[8];

	snprintf(key, sizeof key, "dl.off.%s", kind);
	return !(db_get_str(db_dev(), key, v, sizeof v, NULL) && !strcmp(v, "1"));
}

static void kind_set(const char *kind, bool in)
{
	char key[48];

	snprintf(key, sizeof key, "dl.off.%s", kind);
	if (in) db_del(db_dev(), key);
	else db_set_str(db_dev(), key, "1");
}

static scraper_kinds kinds_now(void)
{
	return (scraper_kinds){ .covers = downloads_kind_in("covers"), .videos = downloads_kind_in("videos"),
	                        .names = scraper_names_on() };
}

/* What each row says, counted as the Scraper's and the achievements
 * download's pages count: when the page opens, after a row changes or a page
 * it opened closes, and as a lane ends - never a frame, since a count reads
 * every system's folder */
static void dl_count(dl_ctx *c)
{
	int l;

	scraper_plan_of(c->a, kinds_now(), &c->sc);
	radl_plan_of(c->a, &c->rc);
	for (l = 0; l < DLQ_LANES; l++) c->ends[l] = dlq_ends((dlq_lane)l);
	c->counted = true;
}

static bool ach_in(const dl_ctx *c)
{
	return downloads_kind_in("ach") && c->rc.in;
}

static void running(char *out, size_t n, int pct)
{
	snprintf(out, n, T(S_DOWNLOADS_RUNNING), pct < 0 ? 0 : pct > 100 ? 100 : pct);
}

/* A kind's row while its lane runs: how far, the number alone (Download's
 * row over it says what is going on), so a long name keeps its room */
static void running_pct(char *out, size_t n, int pct)
{
	snprintf(out, n, "%d%%", pct < 0 ? 0 : pct > 100 ? 100 : pct);
}

/* What the rows say depends on, besides the counts: the lanes running and
 * how far, the network, the kinds in Download. The page reads them live; a
 * shot hands its own (downloads_shot). */
typedef struct {
	bool art, ra, net;
	int pct, art_pct, ra_pct;
	scraper_kinds k;
	bool ach_in;
} dl_now;

static dl_now now_live(void)
{
	return (dl_now){ .art = dlq_running(DLQ_ART), .ra = dlq_running(DLQ_RA), .net = net_online(),
	                 .pct = dlq_pct(), .art_pct = dlq_lane_pct(DLQ_ART), .ra_pct = dlq_lane_pct(DLQ_RA),
	                 .k = kinds_now(), .ach_in = downloads_kind_in("ach") };
}

static void dl_values(dl_ctx *c, const dl_now *n)
{
	const scraper_counts *s = &c->sc;
	const radl_counts *r = &c->rc;
	scraper_kinds k = n->k;
	bool art = n->art, ra = n->ra;

	/* Download: what every kind that is in adds and about how long it
	 * takes, the two servers side by side */
	if (art || ra) {
		running(c->val[DL_DOWNLOAD], sizeof c->val[0], n->pct);
	} else if (!n->net) {
		snprintf(c->val[DL_DOWNLOAD], sizeof c->val[0], "%s", T(S_MENU_NEEDS_WIFI));
	} else {
		bool any_art = s->covers + s->videos + s->looks > 0;
		bool any_ra = n->ach_in && r->in && r->todo > 0;
		double bytes = (any_art ? s->bytes : 0) + (any_ra ? r->bytes : 0);
		double secs = any_art ? s->secs : 0;
		char sz[24], tm[32];

		if (any_ra && r->secs > secs) secs = r->secs;
		if (!any_art && !any_ra) {
			snprintf(c->val[DL_DOWNLOAD], sizeof c->val[0], "%s", T(S_DOWNLOADS_NOTHING));
		} else {
			/* The size and the time, the room a row has: the time is the
			 * page's guess, said as the Scraper's plan says it */
			sys_menu_bytes((unsigned long long)bytes, sz, sizeof sz);
			scraper_time_text(secs, tm, sizeof tm);
			snprintf(c->val[DL_DOWNLOAD], sizeof c->val[0], "%s \xc2\xb7 %s", sz, tm);
		}
	}

	/* The kinds: Off when out of Download (or no system's switch is on),
	 * the run's progress while it goes, else what it lacks */
	if (!k.covers || !s->cov_systems) snprintf(c->val[DL_COVERS], sizeof c->val[0], "%s", T(S_SCRAPER_OFF));
	else if (art) running_pct(c->val[DL_COVERS], sizeof c->val[0], n->art_pct);
	else if (!s->covers) snprintf(c->val[DL_COVERS], sizeof c->val[0], "%s", T(S_SCRAPER_COMPLETE));
	else snprintf(c->val[DL_COVERS], sizeof c->val[0], TN(S_DOWNLOADS_MISSING_N, s->covers), s->covers);

	if (!k.videos || !s->vid_systems) snprintf(c->val[DL_VIDEOS], sizeof c->val[0], "%s", T(S_SCRAPER_OFF));
	else if (art) running_pct(c->val[DL_VIDEOS], sizeof c->val[0], n->art_pct);
	else if (!s->ss_in) snprintf(c->val[DL_VIDEOS], sizeof c->val[0], "%s", T(S_DOWNLOADS_NEEDS_SS));
	else if (!s->videos) snprintf(c->val[DL_VIDEOS], sizeof c->val[0], "%s", T(S_SCRAPER_COMPLETE));
	else snprintf(c->val[DL_VIDEOS], sizeof c->val[0], TN(S_DOWNLOADS_TO_FETCH_N, s->videos), s->videos);

	if (!k.names) snprintf(c->val[DL_NAMES], sizeof c->val[0], "%s", T(S_SCRAPER_OFF));
	else if (art) running_pct(c->val[DL_NAMES], sizeof c->val[0], n->art_pct);
	else if (!s->ss_in) snprintf(c->val[DL_NAMES], sizeof c->val[0], "%s", T(S_DOWNLOADS_NEEDS_SS));
	else if (!s->names) snprintf(c->val[DL_NAMES], sizeof c->val[0], "%s", T(S_SCRAPER_COMPLETE));
	else snprintf(c->val[DL_NAMES], sizeof c->val[0], TN(S_SCRAPER_NAMES_N, s->names), s->names);

	if (!r->in) snprintf(c->val[DL_ACH], sizeof c->val[0], "%s", T(S_SYSMENU_SIGN_IN));
	else if (!n->ach_in) snprintf(c->val[DL_ACH], sizeof c->val[0], "%s", T(S_SCRAPER_OFF));
	else if (ra) running_pct(c->val[DL_ACH], sizeof c->val[0], n->ra_pct);
	else if (!r->todo) snprintf(c->val[DL_ACH], sizeof c->val[0], "%s", T(S_DOWNLOADS_READY));
	else snprintf(c->val[DL_ACH], sizeof c->val[0], TN(S_DOWNLOADS_TO_FETCH_N, r->todo), r->todo);
}

static int dl_rows(dl_ctx *c, menu_row *rows, const char **heading)
{
	*heading = T(S_DOWNLOADS_HEADING);
	rows[DL_DOWNLOAD] = (menu_row){ T(S_RADL_KEY_DOWNLOAD), c->val[DL_DOWNLOAD], true };
	rows[DL_RULE1] = MENU_RULE;
	rows[DL_COVERS] = (menu_row){ T(S_SCRAPER_PAGE_COVERS), c->val[DL_COVERS], true };
	rows[DL_VIDEOS] = (menu_row){ T(S_SCRAPER_PAGE_VIDEOS), c->val[DL_VIDEOS], true };
	rows[DL_NAMES] = (menu_row){ T(S_SCRAPER_NAMES_DETAILS), c->val[DL_NAMES], true };
	rows[DL_ACH] = (menu_row){ T(S_SYSMENU_CHEEVOS), c->val[DL_ACH], true };
	rows[DL_RULE2] = MENU_RULE;
	rows[DL_SETTINGS] = (menu_row){ T(S_HOME_SETTINGS), NULL, true };
	return DL_ROWS;
}

static int dl_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	dl_ctx *c = ctx;
	dl_now n = now_live();
	int l;

	(void)max;
	if (!c->counted) dl_count(c);
	/* A lane that ended changed the card: counted again */
	for (l = 0; l < DLQ_LANES; l++)
		if (c->ends[l] != dlq_ends((dlq_lane)l)) { dl_count(c); break; }
	dl_values(c, &n);
	return dl_rows(c, rows, heading);
}

/* Download: every kind that is in, each on its lane (src/dlq.h). The room
 * is the Scraper's rule: its plan must fit with 200 MB to spare. */
static menu_result dl_download(app *a, dl_ctx *c)
{
	scraper_kinds k = kinds_now();
	bool art, ra;

	if (dlq_busy()) return MENU_STAY;
	if (!net_online()) {
		result_message(a, T(S_DOWNLOADS_HEADING), MSG_NEEDS_WIFI);
		return MENU_FAILED;
	}
	dl_count(c);
	art = c->sc.covers + c->sc.videos + c->sc.looks > 0;
	ra = ach_in(c) && c->rc.todo > 0;
	if (art && !c->sc.fits) {
		result_message(a, T(S_DOWNLOADS_HEADING), T(S_SCRAPER_NO_ROOM));
		return MENU_FAILED;
	}
	if (art) art = scraper_run(a, k);
	if (ra) ra = radl_run(a);
	if (!art && !ra) return MENU_FAILED;
	return MENU_CHANGED;
}

/* ---- Settings: the accounts, what Download gets, Video Snaps ----------- *
 * Each kind in or out of Download is a switch here, On or Off as every
 * MENU switch is (a row that steps opens its list on A, DESIGN.md 11.3);
 * Names and Details is the Scraper's own switch, the same as on its page. */
enum { DSET_SS, DSET_RA, DSET_SG, DSET_RULE, DSET_COVERS, DSET_VIDEOS, DSET_NAMES, DSET_ACH,
       DSET_RULE2, DSET_SNAPS, DSET_ROWS };

typedef struct {
	char ss[64], ra[64], sg[32];
	unsigned char sound[DSET_ROWS];
	menu_choice choice[DSET_ROWS];
	const char *onoff[2];
} dl_set;

static bool set_on(int row)
{
	switch (row) {
	case DSET_COVERS: return downloads_kind_in("covers");
	case DSET_VIDEOS: return downloads_kind_in("videos");
	case DSET_NAMES:  return scraper_names_on();
	case DSET_ACH:    return downloads_kind_in("ach");
	default:          return false;
	}
}

static void set_flip(int row)
{
	switch (row) {
	case DSET_COVERS: kind_set("covers", !downloads_kind_in("covers")); break;
	case DSET_VIDEOS: kind_set("videos", !downloads_kind_in("videos")); break;
	case DSET_NAMES:  scraper_names_set(!scraper_names_on()); break;
	case DSET_ACH:    kind_set("ach", !downloads_kind_in("ach")); break;
	default: break;
	}
}

static int set_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	dl_set *s = ctx;

	(void)max;
	*heading = T(S_HOME_SETTINGS);
	if (!ss_have_dev()) snprintf(s->ss, sizeof s->ss, "%s", T(S_SYSMENU_NOT_IN_BUILD));
	else if (ss_signed_in()) snprintf(s->ss, sizeof s->ss, T(S_SYSMENU_SC_LINKED_AS), ss_user());
	else snprintf(s->ss, sizeof s->ss, "%s", T(S_SYSMENU_SC_LINK));
	if (ra_signed_in()) snprintf(s->ra, sizeof s->ra, "%s", ra_user());
	else snprintf(s->ra, sizeof s->ra, "%s", T(S_SYSMENU_SIGN_IN));
	if (!sgdb_have_key()) snprintf(s->sg, sizeof s->sg, "%s", T(S_SYSMENU_SC_LINK));
	else if (sgdb_key_state() == SGDB_REFUSED) snprintf(s->sg, sizeof s->sg, "%s", T(S_SCRAPER_KEY_REFUSED));
	else sgdb_key_masked(s->sg, sizeof s->sg);
	rows[DSET_SS] = (menu_row){ "ScreenScraper", s->ss, true };
	rows[DSET_RA] = (menu_row){ "RetroAchievements", s->ra, true };
	rows[DSET_SG] = (menu_row){ "SteamGridDB", s->sg, true };
	rows[DSET_RULE] = MENU_RULE;
	rows[DSET_COVERS] = (menu_row){ T(S_SCRAPER_PAGE_COVERS), NULL, true };
	rows[DSET_VIDEOS] = (menu_row){ T(S_SCRAPER_PAGE_VIDEOS), NULL, true };
	rows[DSET_NAMES] = (menu_row){ T(S_SCRAPER_NAMES_DETAILS), NULL, true };
	rows[DSET_ACH] = (menu_row){ T(S_SYSMENU_CHEEVOS), NULL, true };
	rows[DSET_RULE2] = MENU_RULE;
	rows[DSET_SNAPS] = (menu_row){ T(S_SNAPS_HEADING), NULL, true };
	s->onoff[0] = T(S_SCRAPER_ON);
	s->onoff[1] = T(S_SCRAPER_OFF);
	memset(s->sound, 0, sizeof s->sound);
	memset(s->choice, 0, sizeof s->choice);
	for (int r = DSET_COVERS; r <= DSET_ACH; r++) {
		rows[r].value = set_on(r) ? s->onoff[0] : s->onoff[1];
		s->sound[r] = MENU_SND_RING;
		s->choice[r] = (menu_choice){ .n = 2, .cur = set_on(r) ? 0 : 1, .labels = s->onoff };
	}
	return DSET_ROWS;
}

static menu_result set_key(app *a, void *ctx, in_button key, int sel)
{
	(void)ctx;
	/* A switch: Left and Right (and A, which the runner sends as one) */
	if (sel >= DSET_COVERS && sel <= DSET_ACH) {
		if (key != IN_LEFT && key != IN_RIGHT) return MENU_STAY;
		set_flip(sel);
		return MENU_CHANGED;
	}
	if (key != IN_ACCEPT) return MENU_STAY;
	switch (sel) {
	case DSET_SS:    return ss_page(a);
	case DSET_RA:    return ra_signin_screen(a);
	case DSET_SG:    return sgdb_screen(a);
	case DSET_SNAPS: return snaps_screen(a);
	default:       return MENU_STAY;
	}
}

static void dl_settings(app *a)
{
	static dl_set s;

	memset(&s, 0, sizeof s);
	menu_run(a, &(menu_style){ .accent = MENU_ZYPEROS, .fixed_w = menu_std_width(a), .sound = s.sound,
	                           .choices = s.choice },
	         set_build, set_key, &s);
}

static menu_result dl_key(app *a, void *ctx, in_button key, int sel)
{
	dl_ctx *c = ctx;
	menu_result r = MENU_STAY;

	if (key != IN_ACCEPT) return MENU_STAY;
	switch (sel) {
	case DL_DOWNLOAD: r = dl_download(a, c); break;
	/* A kind opens its own page: today's Scraper page has covers, videos
	 * and names in one; the achievements download's has achievements */
	case DL_COVERS: case DL_VIDEOS: case DL_NAMES: scraper_page(a, NULL); break;
	case DL_ACH:
		if (!c->rc.in) ra_signin_screen(a);
		else radl_screen(a);
		break;
	case DL_SETTINGS: dl_settings(a); break;
	default: return MENU_STAY;
	}
	dl_count(c);
	c->said = MENU_CHANGED;
	return r;
}

menu_result downloads_screen(app *a)
{
	static dl_ctx c;

	memset(&c, 0, sizeof c);
	c.a = a;
	c.said = MENU_STAY;
	menu_run(a, &(menu_style){ .accent = MENU_ZYPEROS, .fixed_w = menu_std_width(a) },
	         dl_build, dl_key, &c);
	return c.said;
}

void downloads_rows_report(app *a)
{
	static dl_ctx c;
	menu_row rows[DL_ROWS];
	const char *h;
	int n;

	memset(&c, 0, sizeof c);
	c.a = a;
	n = dl_build(&c, rows, DL_ROWS, &h);
	menu_rows_print("downloads", rows, n, NULL, NULL);
	{
		static dl_set st;
		menu_row srows[DSET_ROWS];

		memset(&st, 0, sizeof st);
		n = set_build(&st, srows, DSET_ROWS, &h);
		menu_rows_print("downloads-settings", srows, n, st.sound, st.choice);
	}
}

/* ---- a shot (--downloads STATE) ------------------------------------------
 * The page on stand-in counts, nothing read: 0 the plan, 1 a run going on,
 * 2 every kind done or out (Videos needing an account, Names off,
 * achievements ready offline), 3 no network and nobody signed in, 4 its
 * Settings. The Scraper's and the achievements download's own estimates. */
int shot_downloads = -1;

void downloads_shot(app *a)
{
	static dl_ctx c;
	menu_row rows[DSET_ROWS > DL_ROWS ? DSET_ROWS : DL_ROWS];
	const char *h;
	int st = shot_downloads, n;
	dl_now now = { .net = st != 3, .k = { .covers = true, .videos = true, .names = st != 2 }, .ach_in = true };

	memset(&c, 0, sizeof c);
	c.a = a;
	if (st == 4) {
		static dl_set s;

		memset(&s, 0, sizeof s);
		n = set_build(&s, rows, DSET_ROWS, &h);
		ui_scrim(a->r, 0);
		menu_draw(a, h, rows, n, DSET_COVERS, menu_std_width(a), MENU_ZYPEROS);
		return;
	}
	c.sc = (scraper_counts){ .covers = 41, .videos = 63, .names = 199, .looks = 262, .cov_systems = 9,
	                         .vid_systems = 3, .bytes = 168e6, .secs = 732, .net = st != 3, .ss_in = true,
	                         .fits = true };
	c.rc = (radl_counts){ .ready = 430, .todo = 150, .none = 60, .bytes = 18e6, .secs = 213, .in = st != 3,
	                      .net = st != 3 };
	if (st == 1) {
		now.art = now.ra = true;
		now.pct = 41;
		now.art_pct = 37;
		now.ra_pct = 52;
	}
	if (st == 2) {
		c.sc = (scraper_counts){ .cov_systems = 9, .vid_systems = 3, .net = true, .fits = true };
		c.rc.todo = 0;
		c.rc.bytes = c.rc.secs = 0;
	}
	dl_values(&c, &now);
	n = dl_rows(&c, rows, &h);
	ui_scrim(a->r, 0);
	menu_draw(a, h, rows, n, st == 2 ? DL_VIDEOS : DL_DOWNLOAD, menu_std_width(a), MENU_ZYPEROS);
}
