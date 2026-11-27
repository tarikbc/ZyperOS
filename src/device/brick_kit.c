/* SPDX-License-Identifier: GPL-3.0-only */
/* The TrimUI Brick's test kit (src/kit.h; docs/building.md, "Test kits"):
 * linked only into a kit, never into a release. What kit 1.0.7-kit.1's
 * tester runs, from the Brick parity plan:
 *
 *   - Hardware Check, Y on MENU > About (kit_page). Two rows, each started
 *     only by the tester's A, every step logged for the log pack.
 *       Rumble motor: the motor's rail read first and never set (the
 *       AXP2202's BLDO3, which TrimUI's own kernel takes for its motor);
 *       only with it on, PH3 (GPIO 227) high for 300 ms, then low. No node
 *       of the device tree uses the pin; it is made an output that starts
 *       low, so it is never high unasked. Never by itself, and no voltage
 *       is set.
 *       Lights: each LED of the A133's LED controller white alone for 1 s,
 *       in the kernel's order, then all off, so the tester can say which lit
 *       where. Kit 1's tester saw none light and the page stop for 23 s (one
 *       usleep a light, on the page's own loop). So the walk is now steps
 *       the page's loop takes as they come due (brick_kit_walk.h): the row
 *       says "Light N of 23" as it goes, A again or leaving the page stops
 *       it, and a stop turns every lit channel off. And it powers the LEDs
 *       first: TrimUI's own kernel (the Smart Pro's, read) requests GPIO 107
 *       (PD11) as "led_5v" in its LED driver's probe and drives it high, and
 *       low at suspend and shutdown; ours, Allwinner's driver as Creality
 *       published it, drives nothing there, and no node of our device tree
 *       uses the pin. Driven high for the walk only, low at its end or
 *       stop, never by itself. Every step is logged: the 5 V pin before and
 *       after, each write and the level the kernel then holds (the LED
 *       class's stored value, not the light's), and the LEDs' facts at the
 *       start.
 *   - The AXP2202's registers in the log pack's hardware report
 *     (kit_hw_facts, after src/device/brick_hw.c's own facts), read only,
 *     from its regmap file in debugfs, which the image does not mount:
 *     mounted read-only for the read and unmounted after when it was not
 *     there before. Nothing in debugfs or the PMIC is written. Beside them
 *     the LEDs' facts (hc_led_facts), read only too.
 *   - Diatom's present times in its log (diatom/port/brick.c's gl_init):
 *     the file seeded with "auto" at the launcher's start where there is
 *     none, in this handheld's own settings folder (.userdata/brick, or
 *     .userdata/smartpro on a Smart Pro, which runs this kit too;
 *     diatom/port/present_file.h). "auto" forces no path; any file turns
 *     the times on.
 *
 * The GPU path's shaders and consoles are on in a kit too, in the Brick's
 * profile (src/device/brick_profile.c).
 *
 * English only, with no strings in res/strings: a kit's page is for its
 * tester and leaves with the kit, so it stays out of the translations and
 * their checks (tools/i18n.py reads S_ IDs, and this file has none). */
#include "../kit.h"
#include "brick_kit_walk.h"
#include "hwfacts.h"
#include "panel.h"
#include "platform.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/mount.h>
#endif

#define HC_GPIO      227        /* PH3: port H (7) x 32 + 3 */
#define HC_PULSE_US  300000
/* The LEDs' 5 V: PD11, port D (3) x 32 + 11, TrimUI's "led_5v" */
#define HC_LED5V     107
#define DEBUGFS      "/sys/kernel/debug"

typedef struct {
	char motor[96];   /* the motor row's value */
	char lights[96];  /* the lights row's value */
	/* The lights walk (brick_kit_walk.h): the LED devices' names, the LEDs
	 * among them, and the walk the page's loop steps */
	char    names[KW_NAMES][KW_NAME];
	int     nnames;
	kw_led  leds[KW_LEDS];
	int     nleds;
	kw_walk walk;
	bool    none;     /* A found no LEDs */
} hc_page;

static int hc_put(const char *path, const char *text)
{
	FILE *f = fopen(path, "w");
	int ok;

	if (!f) return -1;
	ok = fputs(text, f) >= 0 ? 0 : -1;
	if (fclose(f) != 0) ok = -1;
	return ok;
}

static void hc_get(const char *path, char *out, size_t n)
{
	FILE *f = fopen(path, "r");

	out[0] = 0;
	if (f) {
		if (!fgets(out, (int)n, f)) out[0] = 0;
		fclose(f);
	}
	out[strcspn(out, "\n")] = 0;
}

/* BLDO3 by its regulator name: its microvolts and whether it is on */
static bool hc_rail(long *uv, bool *on)
{
	DIR *d = opendir("/sys/class/regulator");
	struct dirent *e;
	bool found = false;

	*uv = 0;
	*on = false;
	while (d && (e = readdir(d))) {
		char path[300], v[48];

		if (e->d_name[0] == '.') continue;
		snprintf(path, sizeof path, "/sys/class/regulator/%s/name", e->d_name);
		hc_get(path, v, sizeof v);
		if (strcmp(v, "axp2202-bldo3")) continue;
		snprintf(path, sizeof path, "/sys/class/regulator/%s/microvolts", e->d_name);
		hc_get(path, v, sizeof v);
		*uv = atol(v);
		snprintf(path, sizeof path, "/sys/class/regulator/%s/state", e->d_name);
		hc_get(path, v, sizeof v);
		*on = !strcmp(v, "enabled");
		found = true;
		break;
	}
	if (d) closedir(d);
	return found;
}

static void hc_motor(hc_page *pg)
{
	char path[64];
	long uv;
	bool on;

	if (!hc_rail(&uv, &on)) {
		fprintf(stderr, "hwcheck: motor: no axp2202-bldo3 regulator: not pulsed\n");
		snprintf(pg->motor, sizeof pg->motor, "No rail found");
		return;
	}
	fprintf(stderr, "hwcheck: motor: rail axp2202-bldo3 %ld uV, %s\n", uv, on ? "enabled" : "off");
	if (!on || uv <= 0) {
		fprintf(stderr, "hwcheck: motor: the rail is off: not pulsed\n");
		snprintf(pg->motor, sizeof pg->motor, "Rail off (%ld uV)", uv);
		return;
	}
	snprintf(path, sizeof path, "/sys/class/gpio/gpio%d/value", HC_GPIO);
	if (access(path, F_OK) != 0) {
		char n[8];

		snprintf(n, sizeof n, "%d", HC_GPIO);
		hc_put("/sys/class/gpio/export", n);
	}
	snprintf(path, sizeof path, "/sys/class/gpio/gpio%d/direction", HC_GPIO);
	/* "low": an output, low, in one step, so the pin is never high unasked */
	if (hc_put(path, "low") != 0) {
		fprintf(stderr, "hwcheck: motor: gpio%d would not be an output: not pulsed\n", HC_GPIO);
		snprintf(pg->motor, sizeof pg->motor, "GPIO %d refused", HC_GPIO);
		return;
	}
	snprintf(path, sizeof path, "/sys/class/gpio/gpio%d/value", HC_GPIO);
	hc_put(path, "1");
	usleep(HC_PULSE_US);
	hc_put(path, "0");
	fprintf(stderr, "hwcheck: motor: PH3 (gpio%d) high for %d ms at %ld uV, then low\n",
	        HC_GPIO, HC_PULSE_US / 1000, uv);
	snprintf(pg->motor, sizeof pg->motor, "Pulsed 300 ms at %.2f V", uv / 1e6);
}

/* GPIO `g`'s sysfs state into out: "out, 1", or "not exported" */
static void hc_gpio_state(int g, char *out, size_t n)
{
	char path[64], dir[16], val[16];

	snprintf(path, sizeof path, "/sys/class/gpio/gpio%d/direction", g);
	if (access(path, F_OK) != 0) {
		snprintf(out, n, "not exported");
		return;
	}
	hc_get(path, dir, sizeof dir);
	snprintf(path, sizeof path, "/sys/class/gpio/gpio%d/value", g);
	hc_get(path, val, sizeof val);
	snprintf(out, n, "%s, %s", dir[0] ? dir : "-", val[0] ? val : "-");
}

/* The LEDs' 5 V on or off: PD11 an output, high or low in one step, so it is
 * never high unasked. Exported first where it is not. False where sysfs
 * refused. */
static bool hc_power(bool on)
{
	char path[64], before[32], after[32];
	bool ok;

	snprintf(path, sizeof path, "/sys/class/gpio/gpio%d/direction", HC_LED5V);
	if (access(path, F_OK) != 0) {
		char n[8];

		snprintf(n, sizeof n, "%d", HC_LED5V);
		hc_put("/sys/class/gpio/export", n);
	}
	hc_gpio_state(HC_LED5V, before, sizeof before);
	ok = hc_put(path, on ? "high" : "low") == 0;
	hc_gpio_state(HC_LED5V, after, sizeof after);
	fprintf(stderr, "hwcheck: lights: 5 V %s: gpio%d (PD11) was %s, now %s%s\n", on ? "on" : "off",
	        HC_LED5V, before, after, ok ? "" : ": refused");
	return ok;
}

/* The LED devices' names, in readdir's order, and the LEDs among them */
static void hc_leds_read(hc_page *pg)
{
	DIR *d = opendir("/sys/class/leds");
	struct dirent *e;

	pg->nnames = 0;
	while (d && (e = readdir(d)) && pg->nnames < KW_NAMES)
		if (e->d_name[0] != '.') snprintf(pg->names[pg->nnames++], KW_NAME, "%.47s", e->d_name);
	if (d) closedir(d);
	pg->nleds = kw_group((const char (*)[KW_NAME])pg->names, pg->nnames, pg->leds, KW_LEDS);
}

/* An LED device's file as a number, -1 where unread */
static int hc_led_num(const char *name, const char *file)
{
	char path[128], v[16];

	snprintf(path, sizeof path, "/sys/class/leds/%s/%s", name, file);
	hc_get(path, v, sizeof v);
	return v[0] ? atoi(v) : -1;
}

/* A debugfs file's text, its lines joined with a space, into out */
static void hc_dbg_text(const char *path, char *out, size_t n)
{
	char *p;

	hwf_text(path, out, n);
	while ((p = strchr(out, '\n'))) {
		if (!p[1]) { *p = 0; break; }
		*p = ' ';
	}
}

/* The first line of `path` with `needle` in it, trimmed, into out; "" for none */
static void hc_line_with(const char *path, const char *needle, char *out, size_t n)
{
	FILE *f = fopen(path, "r");
	char l[256];

	out[0] = 0;
	while (f && fgets(l, sizeof l, f)) {
		char *s = l;

		if (!strstr(l, needle)) continue;
		while (*s == ' ' || *s == '\t') s++;
		s[strcspn(s, "\n")] = 0;
		snprintf(out, n, "%s", s);
		break;
	}
	if (f) fclose(f);
}

/* debugfs mounted read-only where it is not; true when it is there, and
 * `here` when this mounted it (hc_debugfs_close unmounts it then) */
static bool hc_debugfs_open(bool *here)
{
	*here = false;
#ifdef __linux__
	if (access(DEBUGFS "/regmap", F_OK) == 0 || access(DEBUGFS "/gpio", F_OK) == 0) return true;
	if (mount("debugfs", DEBUGFS, "debugfs", MS_RDONLY, NULL) != 0) return false;
	*here = true;
	return true;
#else
	return false;
#endif
}

static void hc_debugfs_close(bool here)
{
#ifdef __linux__
	if (here) umount2(DEBUGFS, MNT_DETACH);
#else
	(void)here;
#endif
}

/* How many lines of the kernel's log have `needle` */
static int hc_klog_count(const char *klog, const char *needle)
{
	const char *p = klog;
	int n = 0;

	while ((p = strstr(p, needle))) {
		n++;
		p += strlen(needle);
	}
	return n;
}

/* The lights' facts, a line each, into out, all read only: the LEDs and the
 * levels the kernel holds for them, the 5 V pin, TrimUI's led_anim class if
 * this kernel has one (ours has not), and with debugfs up the LED
 * controller's own files (its hwversion is a register: a version and not
 * r0p0 says its clock is on), the pin in gpiolib's list and pinctrl's, and
 * the LED driver's errors in the kernel's log */
static void hc_led_facts(hc_page *pg, char *out, size_t n, bool debugfs)
{
	static char klog[1 << 18];
	char v[512], w[160];
	size_t at = 0;
	int i, lo = -1, hi = -1;
	DIR *d;
	struct dirent *e;

#define LINE(...) do { \
		int k_ = snprintf(out + at, n - at, __VA_ARGS__); \
		if (k_ > 0) at += (size_t)k_ < n - at ? (size_t)k_ : n - at - 1; \
	} while (0)
	if (!n) return;
	out[0] = 0;
	hc_leds_read(pg);
	for (i = 0; i < pg->nnames; i++) {
		int m = hc_led_num(pg->names[i], "max_brightness");

		if (lo < 0 || m < lo) lo = m;
		if (m > hi) hi = m;
	}
	if (!pg->nnames) LINE("LEDs: none in /sys/class/leds\n");
	else if (lo == hi) LINE("LEDs: %d (%d devices, max_brightness %d)\n", pg->nleds, pg->nnames, hi);
	else LINE("LEDs: %d (%d devices, max_brightness %d to %d)\n", pg->nleds, pg->nnames, lo, hi);
	if (pg->nleds) {
		LINE("Held:");
		for (i = 0; i < pg->nleds; i++) {
			int c;

			LINE(" %s", pg->leds[i].num >= 0 ? "" : pg->leds[i].stem);
			if (pg->leds[i].num >= 0) LINE("%d", pg->leds[i].num);
			for (c = 0; c < pg->leds[i].nch; c++)
				LINE("%s%d", c ? "/" : ":", hc_led_num(pg->names[pg->leds[i].ch[c]], "brightness"));
		}
		LINE("\n");
	}
	hc_gpio_state(HC_LED5V, w, sizeof w);
	LINE("5 V pin: gpio%d (PD11) %s\n", HC_LED5V, w);
	if ((d = opendir("/sys/class/led_anim"))) {
		LINE("led_anim:");
		while ((e = readdir(d)))
			if (e->d_name[0] != '.') LINE(" %s", e->d_name);
		closedir(d);
		hc_get("/sys/class/led_anim/max_scale", w, sizeof w);
		if (w[0]) LINE("; max_scale %s", w);
		hc_get("/sys/class/led_anim/led_enable", w, sizeof w);
		if (w[0]) LINE("; led_enable %s", w);
		LINE("\n");
	} else {
		LINE("led_anim: none\n");
	}
	if (debugfs) {
		hc_get(DEBUGFS "/sunxi_leds/hwversion", v, sizeof v);
		hc_get(DEBUGFS "/sunxi_leds/output_mode", w, sizeof w);
		LINE("Controller: hwversion %s, output_mode %s\n", v[0] ? v : "-", w[0] ? w : "-");
		hc_dbg_text(DEBUGFS "/sunxi_leds/data", v, sizeof v);
		LINE("Controller data: %s\n", v[0] ? v : "-");
		snprintf(w, sizeof w, "gpio-%d ", HC_LED5V);
		hc_line_with(DEBUGFS "/gpio", w, v, sizeof v);
		LINE("gpiolib: %s\n", v[0] ? v : "gpio 107 not requested");
		v[0] = 0;
		if ((d = opendir(DEBUGFS "/pinctrl"))) {
			while (!v[0] && (e = readdir(d))) {
				char path[300];

				if (e->d_name[0] == '.') continue;
				snprintf(path, sizeof path, DEBUGFS "/pinctrl/%s/pinmux-pins", e->d_name);
				hc_line_with(path, "(PD11)", v, sizeof v);
			}
			closedir(d);
		}
		LINE("pinctrl: %s\n", v[0] ? v : "PD11 not listed");
	}
	hw_klog_text(klog, sizeof klog);
	LINE("Kernel log: %d \"led xfer timeout\", %d \"fifo overflow\", %d \"drop this operation\"\n",
	     hc_klog_count(klog, "led xfer timeout"), hc_klog_count(klog, "fifo overflow"),
	     hc_klog_count(klog, "drop this operation"));
#undef LINE
}

/* The facts into the log, a line each */
static void hc_led_facts_log(hc_page *pg)
{
	static char facts[2048];
	bool here, dbg = hc_debugfs_open(&here);
	const char *p = facts;

	hc_led_facts(pg, facts, sizeof facts, dbg);
	hc_debugfs_close(here);
	while (*p) {
		size_t len = strcspn(p, "\n");

		fprintf(stderr, "hwcheck: lights: %.*s\n", (int)len, p);
		p += len;
		if (*p == '\n') p++;
	}
}

/* A: the walk from now, after the facts; every channel's name and limit
 * logged first, so a log says which LED is which channel */
static void hc_lights_start(hc_page *pg)
{
	static unsigned char held[KW_LEDS][3];
	int i, c;

	hc_led_facts_log(pg);
	pg->none = !pg->nleds;
	if (pg->none) {
		fprintf(stderr, "hwcheck: lights: no LEDs in /sys/class/leds: nothing lit\n");
		return;
	}
	for (i = 0; i < pg->nleds; i++) {
		char line[256];
		size_t at = 0;

		for (c = 0; c < pg->leds[i].nch; c++) {
			const char *name = pg->names[pg->leds[i].ch[c]];
			int b = hc_led_num(name, "brightness");

			held[i][c] = (unsigned char)(b < 0 ? 0 : b > 255 ? 255 : b);
			at += (size_t)snprintf(line + at, sizeof line - at, "%s%s (max_brightness %d, holds %d)",
			                       c ? ", " : "", name, hc_led_num(name, "max_brightness"), b);
			if (at >= sizeof line) at = sizeof line - 1;
		}
		fprintf(stderr, "hwcheck: lights: LED %d of %d: %s\n", i + 1, pg->nleds, line);
	}
	fprintf(stderr, "hwcheck: lights: walk: %d LEDs, each %d ms at %d, a write each %d ms, "
	        "the 5 V on gpio%d first\n", pg->nleds, KW_LIT_MS, KW_LEVEL, KW_GAP_MS, HC_LED5V);
	kw_start(&pg->walk, pg->leds, pg->nleds, (const unsigned char (*)[3])held, plat_now_ms());
}

/* One of the walk's steps, done and logged */
static void hc_act(hc_page *pg, const kw_act *a)
{
	char path[128], v[16], back[16];
	const kw_led *l;
	const char *name;
	int ok;

	if (a->kind == KW_ACT_POWER_ON || a->kind == KW_ACT_POWER_OFF) {
		hc_power(a->kind == KW_ACT_POWER_ON);
		if (a->kind == KW_ACT_POWER_OFF)
			fprintf(stderr, "hwcheck: lights: %s, all off\n", pg->walk.state == KW_STOPPED ? "stopped" : "done");
		return;
	}
	l = &pg->leds[a->led];
	name = pg->names[l->ch[a->ch]];
	if (a->value && a->ch == 0)
		fprintf(stderr, "hwcheck: lights: %d of %d: %s on\n", a->led + 1, pg->nleds, l->stem);
	snprintf(path, sizeof path, "/sys/class/leds/%s/brightness", name);
	snprintf(v, sizeof v, "%d", a->value);
	ok = hc_put(path, v);
	/* The LED class's stored value: what the kernel holds, not what lit */
	hc_get(path, back, sizeof back);
	fprintf(stderr, "hwcheck: lights: %s to %d%s, holds %s\n", name, a->value,
	        ok == 0 ? "" : " (refused)", back[0] ? back : "-");
}

/* Each pass of the page's loop, before its frame (menu_style's `at`): the
 * walk's steps that are due, and a wake for the next */
static void hc_tick(app *a, void *ctx, int sel)
{
	hc_page *pg = ctx;
	kw_act act;

	(void)a;
	(void)sel;
	while (kw_next(&pg->walk, plat_now_ms(), &act)) hc_act(pg, &act);
	if (kw_running(&pg->walk)) redraw_at(kw_due(&pg->walk));
}

/* The walk stopped and its steps taken here, waiting between them: the page
 * is going, so nothing else draws meanwhile (the lit channels and the 5 V,
 * a few writes 40 ms apart) */
static void hc_lights_end(hc_page *pg, const char *why)
{
	kw_act act;

	if (!kw_running(&pg->walk)) return;
	fprintf(stderr, "hwcheck: lights: %s: stopping at %d of %d\n", why, pg->walk.shown + 1, pg->nleds);
	kw_stop(&pg->walk, plat_now_ms());
	while (kw_running(&pg->walk)) {
		unsigned now = plat_now_ms(), due = kw_due(&pg->walk);

		if (kw_next(&pg->walk, now, &act)) hc_act(pg, &act);
		else if ((int)(due - now) > 0) usleep((due - now) * 1000u);
	}
}

static int hc_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	hc_page *pg = ctx;

	(void)max;
	*heading = "Hardware Check";
	if (pg->none) snprintf(pg->lights, sizeof pg->lights, "No LEDs found");
	else kw_text(&pg->walk, pg->lights, sizeof pg->lights);
	rows[0] = (menu_row){ "Rumble motor", pg->motor[0] ? pg->motor : "Press A (300 ms)", true };
	rows[1] = (menu_row){ "Lights", pg->lights, true };
	return 2;
}

static menu_result hc_key(app *a, void *ctx, in_button key, int sel)
{
	hc_page *pg = ctx;

	(void)a;
	if (key != IN_ACCEPT) return MENU_STAY;
	if (sel == 0) {
		hc_motor(pg);
	} else if (sel == 1) {
		/* A again while it goes: stop */
		if (kw_running(&pg->walk)) {
			fprintf(stderr, "hwcheck: lights: A: stopping at %d of %d\n", pg->walk.shown + 1, pg->nleds);
			kw_stop(&pg->walk, plat_now_ms());
		} else {
			hc_lights_start(pg);
		}
	}
	return MENU_CHANGED;
}

/* POWER or Auto Off with the walk going: its lights off first, then off as
 * everywhere (a charger's screen in place of off would keep them lit) */
static menu_result hc_power_key(app *a, void *ctx)
{
	hc_lights_end(ctx, "power");
	power_off(a);
	return MENU_DONE;
}

void kit_page(app *a)
{
	static hc_page pg;

	memset(&pg, 0, sizeof pg);
	fprintf(stderr, "hwcheck: open\n");
	menu_run(a, &(menu_style){ .accent = MENU_ZYPEROS, .fixed_w = menu_std_width(a),
	                           .on_power = hc_power_key, .at = hc_tick },
	         hc_build, hc_key, &pg);
	hc_lights_end(&pg, "page left");
}

void kit_hw_facts(hw_facts *f)
{
#ifdef __linux__
	static hc_page pg;
	bool here, dbg = hc_debugfs_open(&here);
	char path[300];
	DIR *d;
	struct dirent *e;

	/* Its I2C address is 0x34 (the device tree's pmu@34), so its regmap's
	 * folder ends in "-0034" */
	snprintf(path, sizeof path, "%s/regmap", DEBUGFS);
	if (dbg && (d = opendir(path))) {
		while ((e = readdir(d))) {
			size_t l = strlen(e->d_name);

			if (l < 5 || strcmp(e->d_name + l - 5, "-0034")) continue;
			snprintf(path, sizeof path, "%s/regmap/%s/registers", DEBUGFS, e->d_name);
			hwf_text(path, f->pmic_regs, sizeof f->pmic_regs);
			break;
		}
		closedir(d);
	}
	hc_led_facts(&pg, f->led_facts, sizeof f->led_facts, dbg);
	hc_debugfs_close(here);
#else
	(void)f;
#endif
}

#ifdef __linux__
/* Diatom's present-time file, where its Brick port reads it: this
 * handheld's settings folder (diatom/port/present_file.h) */
#include "../../diatom/port/present_file.h"

/* At the launcher's start, before main: a card whose userdata folder is
 * there and has no such file gets one, once */
__attribute__((constructor)) static void kit_present_seed(void)
{
	char dir[512], file[600];
	FILE *f;

	present_file_paths(dir, sizeof dir, file, sizeof file);
	if (access(dir, F_OK) != 0 || access(file, F_OK) == 0) return;
	if (!(f = fopen(file, "w"))) return;
	fputs("auto\n", f);
	if (fclose(f) == 0)
		fprintf(stderr, "kit: %s seeded with \"auto\": Diatom's present times on\n", file);
}
#endif
