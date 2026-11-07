/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* The shelf's status corner: what src/status.c decides and draws, without a
 * screen.
 *
 *     make check-status
 *
 * The rules first, at their edges: the battery's low and critical warnings
 * (the same 10% and 5% as Diatom's disc over a game) and that the charger
 * silences both; the fill's width, never under 4 while there is any charge;
 * the signal's steps, -60 and -70 dBm; and wpa_cli's signal_poll read for
 * them, a driver that cannot say read as nothing. Then the marks as the
 * status lab draws them, in BOTH themes and from the design tokens, probed
 * where each shape is solid: the fan's lit and unlit parts, the crossed fan
 * of a radio that is on and not connected and the cut around its slash, the
 * Bluetooth rune, the battery's outline, fill and the air between, low in
 * **warning**, critical with its fill gone for the blink's second off, and
 * charging, the fill and bolt in **accent-ink** and the outline cut away
 * around the bolt. And where the marks stand: 12 apart, the battery last.
 *
 *   status-check                checks
 *   status-check --pam DIR      also writes the marks it drew as PAM images
 *
 * Links src/status.c and NOT SDL. */
#include "../src/design.h"
#include "../src/status.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

static const char *pam_dir;

static void dump(const char *name, const uint32_t *px, int w, int h)
{
	char path[512];
	FILE *f;
	int i;

	if (!pam_dir) return;
	snprintf(path, sizeof path, "%s/%s.pam", pam_dir, name);
	if (!(f = fopen(path, "wb"))) return;
	fprintf(f, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", w, h);
	for (i = 0; i < w * h; i++) {
		unsigned char b[4] = { px[i] >> 16 & 255, px[i] >> 8 & 255, px[i] & 255, px[i] >> 24 };
		fwrite(b, 1, 4, f);
	}
	fclose(f);
}

static void rules(void)
{
	printf("the battery's warnings:\n");
	ck(status_batt_level(100, false) == 0, "full is fine");
	ck(status_batt_level(11, false) == 0, "11% is fine");
	ck(status_batt_level(10, false) == 1, "10% is low");
	ck(status_batt_level(6, false) == 1, "and 6%");
	ck(status_batt_level(5, false) == 2, "5% is critical");
	ck(status_batt_level(0, false) == 2, "and empty");
	ck(status_batt_level(5, true) == 0, "on the charger nothing warns");
	ck(status_batt_level(1, true) == 0, "even at 1%");

	printf("the battery's fill:\n");
	ck(status_batt_fill(0) == 0, "none at 0%");
	ck(status_batt_fill(-1) == 0, "or under");
	ck(status_batt_fill(1) == 4, "4 wide at 1%, so it shows");
	ck(status_batt_fill(9) == 4, "and at 9%");
	ck(status_batt_fill(50) == 14, "half of 28 at 50%");
	ck(status_batt_fill(82) == 23, "23 at 82%");
	ck(status_batt_fill(100) == 28, "28 at 100%");
	ck(status_batt_fill(140) == 28, "and never past it");

	printf("the signal's steps:\n");
	ck(status_wifi_steps(-40) == 3, "-40 dBm is three");
	ck(status_wifi_steps(-60) == 3, "and -60");
	ck(status_wifi_steps(-61) == 2, "-61 is two");
	ck(status_wifi_steps(-70) == 2, "and -70");
	ck(status_wifi_steps(-71) == 1, "-71 is one");
	ck(status_wifi_steps(-95) == 1, "and -95, still connected");

	printf("wpa_cli signal_poll:\n");
	{
		int dbm = 0;

		ck(status_rssi_dbm("RSSI=-56\nLINKSPEED=72\nNOISE=9999\nFREQUENCY=2437\n", &dbm) &&
		   dbm == -56, "RSSI=-56 is -56 dBm");
		dbm = 0;
		ck(status_rssi_dbm("LINKSPEED=150\nRSSI=-71\nAVG_RSSI=-70\n", &dbm) && dbm == -71,
		   "RSSI on any line, and not AVG_RSSI");
		ck(status_rssi_dbm("AVG_RSSI=-40\nRSSI=-63\n", &dbm) && dbm == -63,
		   "even when AVG_RSSI comes first");
		ck(!status_rssi_dbm("FAIL\n", &dbm), "a driver that cannot say gives FAIL, and no signal");
		ck(!status_rssi_dbm("RSSI=-9999\n", &dbm), "nor a level nobody heard");
		ck(!status_rssi_dbm("RSSI=0\n", &dbm), "nor zero");
		ck(!status_rssi_dbm("RSSI=\n", &dbm), "nor nothing");
		ck(!status_rssi_dbm("", &dbm), "an empty answer is none");
		ck(!status_rssi_dbm(NULL, &dbm), "and so is no answer");
	}
}

/* ---- the marks, drawn ------------------------------------------------------ */

static uint32_t px[(STATUS_PAD * 2 + 120) * (STATUS_PAD * 2 + STATUS_ROW)];
static int pw, ph;

static void draw(const status_marks *m, int theme, const char *name)
{
	status_marks_size(m, 1.0f, &pw, &ph);
	if ((size_t)pw * ph > sizeof px / sizeof px[0]) { pw = ph = 0; return; }
	status_marks_render(px, pw, ph, pw, 1.0f, theme, m);
	dump(name, px, pw, ph);
}

/* The pixel whose center is at (x, y) units in the mark that starts x0 units
 * into the row: x and y end in .5, the middle of a pixel at scale 1. */
static uint32_t at(int x0, float x, float y)
{
	int i = STATUS_PAD + x0 + (int)x, j = STATUS_PAD + (int)y;

	if (i < 0 || j < 0 || i >= pw || j >= ph) return 0;
	return px[j * pw + i];
}

static int is(uint32_t p, ds_rgb c)
{
	return p >> 24 == 255 && (p >> 16 & 255) == c.r && (p >> 8 & 255) == c.g &&
	       (p & 255) == c.b;
}

static int clear(uint32_t p) { return p >> 24 == 0; }

static void marks(int theme)
{
	const ds_theme *th = &DS_THEME[theme];
	char name[64], what[160];
	status_marks m;
	int k, x0;

	printf("the marks, %s:\n", th->name);

#define CK(cond, ...) do { snprintf(what, sizeof what, __VA_ARGS__); ck(cond, what); } while (0)

	/* Wi-Fi alone first, so it starts at 0 */
	for (k = 1; k <= 3; k++) {
		m = (status_marks){ .wifi = k, .batt = -1 };
		snprintf(name, sizeof name, "%s-wifi%d", theme ? "dark" : "light", k);
		draw(&m, theme, name);
		CK(pw == STATUS_PAD * 2 + 36, "Wi-Fi alone is 36 wide (%d)", pw);
		CK(is(at(0, 17.5, 20.5), th->text_soft), "%d step(s): the wedge is lit, text-soft", k);
		CK(is(at(0, 17.5, 10.5), k >= 2 ? th->text_soft : th->track),
		   "%d step(s): the inner arc is %s", k, k >= 2 ? "lit" : "track");
		CK(is(at(0, 17.5, 2.5), k >= 3 ? th->text_soft : th->track),
		   "%d step(s): the outer arc is %s", k, k >= 3 ? "lit" : "track");
		CK(clear(at(0, 17.5, 13.5)), "%d step(s): air between the wedge and the arc", k);
		CK(is(at(0, 5.5, 5.5), k >= 3 ? th->text_soft : th->track),
		   "%d step(s): the outer arc near its left end", k);
	}
	m = (status_marks){ .wifi = STATUS_WIFI_IDLE, .batt = -1 };
	draw(&m, theme, theme ? "dark-wifi-idle" : "light-wifi-idle");
	CK(is(at(0, 17.5, 20.5), th->text_dim), "not connected: the wedge in text-dim");
	CK(is(at(0, 17.5, 2.5), th->text_dim), "and the outer arc");
	CK(is(at(0, 6.5, 2.5), th->text_dim), "crossed by the slash");
	CK(clear(at(0, 5.5, 5.5)), "which is cut out of the fan around it");
	m.wifi = STATUS_WIFI_NONE;
	CK(status_marks_width(&m) == 0, "off: no mark at all");

	m = (status_marks){ .wifi = STATUS_WIFI_NONE, .bt = true, .batt = -1 };
	draw(&m, theme, theme ? "dark-bt" : "light-bt");
	CK(pw == STATUS_PAD * 2 + 16, "Bluetooth alone is 16 wide (%d)", pw);
	CK(is(at(0, 7.5, 12.5), th->text_soft), "the rune's stem, text-soft");
	CK(is(at(0, 12.5, 7.5), th->text_soft), "and its upper corner");
	CK(clear(at(0, 1.5, 12.5)), "and nothing beside it");

	/* the battery alone: it starts at 0 */
	m = (status_marks){ .wifi = STATUS_WIFI_NONE, .batt = 82 };
	draw(&m, theme, theme ? "dark-batt82" : "light-batt82");
	CK(pw == STATUS_PAD * 2 + 44, "the battery alone is 44 wide (%d)", pw);
	CK(is(at(0, 1.5, 12.5), th->text_soft), "82%%: the outline, text-soft");
	CK(is(at(0, 42.5, 12.5), th->text_soft), "the tip");
	CK(clear(at(0, 4.5, 12.5)), "air between the outline and the fill");
	CK(is(at(0, 17.5, 12.5), th->text_soft), "the fill");
	CK(clear(at(0, 31.5, 12.5)), "and air past it, 23 of 28");

	m.batt = 9;
	draw(&m, theme, theme ? "dark-batt9" : "light-batt9");
	CK(is(at(0, 1.5, 12.5), th->warning), "9%%: the outline in warning");
	CK(is(at(0, 42.5, 12.5), th->warning), "and the tip");
	CK(is(at(0, 7.5, 12.5), th->warning), "and the fill, 4 wide");
	CK(clear(at(0, 11.5, 12.5)), "and no wider");

	m.batt = 4;
	m.blink_off = true;
	draw(&m, theme, theme ? "dark-batt4-off" : "light-batt4-off");
	CK(is(at(0, 1.5, 12.5), th->warning), "critical, blink off: the outline stays");
	CK(clear(at(0, 7.5, 12.5)), "and the fill is gone");
	m.blink_off = false;
	draw(&m, theme, theme ? "dark-batt4-on" : "light-batt4-on");
	CK(is(at(0, 7.5, 12.5), th->warning), "blink on: the fill is back");

	m.batt = 46;
	m.charging = true;
	draw(&m, theme, theme ? "dark-charging" : "light-charging");
	CK(is(at(0, 1.5, 12.5), th->text_soft), "charging: the outline text-soft, not warning");
	CK(is(at(0, 8.5, 12.5), th->accent_ink), "the fill in accent-ink");
	CK(is(at(0, 24.5, 11.5), th->accent_ink), "and the bolt over the empty part");
	CK(clear(at(0, 15.5, 22.5)), "with the outline cut away around it");
	m.charging = false;
	draw(&m, theme, "scratch");
	CK(is(at(0, 15.5, 22.5), th->text_soft), "where off the charger the outline is whole");
	CK(clear(at(0, 24.5, 11.5)), "and there is no bolt");
	m.batt = 4;
	m.charging = true;
	CK(status_batt_level(m.batt, m.charging) == 0, "4%% on the charger does not warn");
	draw(&m, theme, "scratch");
	CK(is(at(0, 1.5, 12.5), th->text_soft), "so its outline is text-soft");

	/* all of them: Bluetooth, Wi-Fi, the battery, 12 apart */
	m = (status_marks){ .wifi = 3, .bt = true, .batt = 82 };
	draw(&m, theme, theme ? "dark-all" : "light-all");
	CK(status_marks_width(&m) == 16 + 12 + 36 + 12 + 44, "all three are 120 wide");
	x0 = 16 + 12;
	CK(is(at(x0, 17.5, 20.5), th->text_soft), "Wi-Fi 12 after the rune");
	x0 += 36 + 12;
	CK(is(at(x0, 1.5, 12.5), th->text_soft), "and the battery 12 after Wi-Fi");
	CK(is(at(x0, 42.5, 12.5), th->text_soft), "its tip at the row's right end");
	CK(clear(at(0, 22.5, 12.5)), "nothing in a gap");
}

static void layout(void)
{
	status_marks m;
	int w, h;

	printf("where they stand:\n");
	m = (status_marks){ .wifi = STATUS_WIFI_NONE, .batt = -1 };
	ck(status_marks_width(&m) == 0, "no marks, no width");
	m.batt = 50;
	ck(status_marks_width(&m) == 44, "the battery alone, 44");
	m.wifi = 2;
	ck(status_marks_width(&m) == 36 + 12 + 44, "Wi-Fi and the battery, 92");
	m.wifi = STATUS_WIFI_NONE;
	m.bt = true;
	ck(status_marks_width(&m) == 16 + 12 + 44, "Bluetooth and the battery, 72");
	m.batt = -1;
	ck(status_marks_width(&m) == 16, "Bluetooth alone, 16");
	m = (status_marks){ .wifi = 3, .bt = true, .batt = 100 };
	status_marks_size(&m, 0.625f, &w, &h);
	ck(w == 77 && h == 17, "at the Pixel 2's 0.625 the buffer is 77 by 17");
	ck(STATUS_RIGHT == 1000 && STATUS_TOP == 24 && STATUS_MID == 36,
	   "the row ends at x 1000, from y 24, its middle on 36");
	ck(STATUS_GAP == 12 && STATUS_CLOCK_GAP == 16,
	   "marks 12 apart, the clock 16 from them, as the lab drew them");

	/* a theme from a file another process wrote may be anything */
	{
		uint32_t a[sizeof px / sizeof px[0]];

		m = (status_marks){ .wifi = 1, .batt = 9 };
		status_marks_size(&m, 1.0f, &w, &h);
		status_marks_render(a, w, h, w, 1.0f, 7, &m);
		status_marks_render(px, w, h, w, 1.0f, DS_LIGHT, &m);
		ck(!memcmp(a, px, (size_t)w * h * sizeof a[0]), "a theme out of range draws Beard");
	}
}

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "--pam")) pam_dir = argv[2];
	rules();
	marks(DS_LIGHT);
	marks(DS_DARK);
	layout();
	if (fails) { printf("\n%d status check(s) failed\n", fails); return 1; }
	printf("\nok: the corner's rules hold and its marks are drawn as the lab drew them\n");
	return 0;
}
