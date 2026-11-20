/* SPDX-License-Identifier: GPL-3.0-only */
/* The device profiles (src/device/profile.h): every profile filled in, every
 * control it has printed with something, no control twice, and today's
 * answers for the GKD Pixel 2, the TrimUI Brick and the host's `desktop`,
 * which the launcher's pixels depend on (docs/building.md,
 * Devices). The chip's temperature
 * from the zones a profile names, on a sysfs made in a scratch folder. Then
 * the choice of a profile by its id, as --device makes it on the host. Built
 * with all three profiles, as the host build is. No SDL. */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../src/canvas.h"
#include "../src/device/profile.h"
#include "../src/ds/button.h"
#include "../src/thermal.h"
#include "device/build.h"

static int fails, checks;

static void ck(int cond, const char *what)
{
	checks++;
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

static bool near(float a, float b) { return a - b < 0.01f && b - a < 0.01f; }

static void whole(const device_profile *p)
{
	char what[128];
	int i, j;

	snprintf(what, sizeof what, "%s: named, its folders and its controls", p->id ? p->id : "?");
	ck(p->id && *p->id && p->name && *p->name && p->userdata && *p->userdata &&
	   p->boot_label && *p->boot_label && p->buttons && p->n_buttons > 0, what);
	snprintf(what, sizeof what, "%s: About thanks who made its kernel, and for which handheld", p->id);
	ck(p->kernel.by && *p->kernel.by && p->kernel.of && *p->kernel.of &&
	   strlen(p->kernel.of) < 24, what);
	/* The picture is 4:3 everywhere; a 16:9 panel shows it scaled to its
	 * height, bars at each side (src/device/brick.c) */
	snprintf(what, sizeof what, "%s: a 4:3 screen, or a 16:9 one with side bars", p->id);
	ck(p->screen.aspect_w * 3 == p->screen.aspect_h * 4 || p->screen.aspect_w * 9 == p->screen.aspect_h * 16,
	   what);
	snprintf(what, sizeof what, "%s: the panel's own pixels are its aspect", p->id);
	ck((p->screen.turn % 180 ? p->screen.panel_h * p->screen.aspect_h == p->screen.panel_w * p->screen.aspect_w
	                         : p->screen.panel_w * p->screen.aspect_h == p->screen.panel_h * p->screen.aspect_w),
	   what);
	snprintf(what, sizeof what, "%s: a panel and a turn of 0, 90, 180 or 270", p->id);
	ck(p->screen.panel_w > 0 && p->screen.panel_h > 0 && p->screen.turn % 90 == 0 &&
	   p->screen.turn >= 0 && p->screen.turn < 360, what);
	snprintf(what, sizeof what, "%s: the glass's size in millimetres", p->id);
	ck(p->screen.active_w_mm > 0 && p->screen.active_h_mm > 0, what);
	for (i = 0; i < p->n_buttons; i++) {
		const dev_button *b = &p->buttons[i];

		snprintf(what, sizeof what, "%s: button %d printed with something", p->id, i);
		ck(b->label && *b->label, what);
		snprintf(what, sizeof what, "%s: button %d a launcher button", p->id, i);
		ck(b->id >= 0 && b->id < IN_COUNT, what);
		for (j = i + 1; j < p->n_buttons; j++) {
			snprintf(what, sizeof what, "%s: button %d not listed twice", p->id, b->id);
			ck(p->buttons[j].id != b->id, what);
		}
	}
}

static bool label_is(const device_profile *p, in_button b, const char *label)
{
	const dev_button *k = dev_button_of(p, b);

	return k && !strcmp(k->label, label);
}

static void pixel2(void)
{
	const device_profile *p = &device_profile_pixel2;
	const dev_button *menu = dev_button_of(p, IN_MENU);

	ck(!strcmp(p->id, "pixel2") && !strcmp(p->userdata, "pixel2") &&
	   !strcmp(p->boot_label, "PX2BOOT"), "pixel2: its id, folder and boot label");
	ck(p->screen.panel_w == 480 && p->screen.panel_h == 640 && p->screen.turn == 270,
	   "pixel2: a 480x640 panel, turned a quarter (pixel2.c's -90)");
	ck(near(p->screen.active_w_mm, 48.8f) && near(p->screen.active_h_mm, 36.6f),
	   "pixel2: 48.8 x 36.6 mm of glass");
	ck(p->screen.still, "pixel2: a still of the panel can be saved");
	ck(p->chords.bright == DEV_BRIGHT_MENU_VOL && p->chords.menu_on_release &&
	   p->chords.lr2_taps && p->chords.home_chord && p->chords.battery_chord,
	   "pixel2: brightness on MENU+VOL, MENU on release, L2/R2 taps, Home, battery");
	ck(p->model == &td_model_pixel2 && !p->model_borrowed && !p->bluetooth && p->gpu_shaders && p->gpu_frames &&
	   p->speaker_mono && p->player2 && p->usb_shared_port && p->wifi == DEV_WIFI_ADAPTER,
	   "pixel2: its own model, no Bluetooth, GPU shaders and frames, one speaker, Player 2, a shared port, adapter Wi-Fi");
	ck(menu && !strcmp(menu->label, "MENU") && menu->cap == DEV_CAP_ROUND && menu->icon == DEV_ICON_DOTS,
	   "pixel2: MENU is the round key with three dots");
	ck(!dev_button_of(p, IN_BRIGHTUP) && !dev_button_of(p, IN_BRIGHTDN),
	   "pixel2: no brightness keys of its own");
	ck(label_is(p, IN_ACCEPT, "A") && label_is(p, IN_VOLUP, "VOL+") && label_is(p, IN_L2, "L2"),
	   "pixel2: A, VOL+ and L2 as printed");
}

static void brick(void)
{
	const device_profile *p = &device_profile_brick;
	const dev_button *menu = dev_button_of(p, IN_MENU);

	ck(!strcmp(p->id, "brick") && !strcmp(p->userdata, "brick") &&
	   !strcmp(p->boot_label, "BRICKBOOT"), "brick: its id, folder and boot label");
	ck(p->screen.panel_w == 1024 && p->screen.panel_h == 768 && p->screen.turn == 0,
	   "brick: a 1024x768 panel, not turned");
	ck(near(p->screen.active_w_mm, 65.0f) && near(p->screen.active_h_mm, 48.8f),
	   "brick: 65.0 x 48.8 mm of glass");
	ck(p->chords.bright == DEV_BRIGHT_KEYS && !p->chords.menu_on_release &&
	   !p->chords.lr2_taps && !p->chords.home_chord && !p->chords.battery_chord,
	   "brick: brightness on its own keys, no chords of the Pixel 2's");
	ck(p->model == &td_model_brick && !p->model_borrowed && p->bluetooth && !p->gpu_shaders && !p->gpu_frames &&
	   !p->speaker_mono && !p->player2 && !p->usb_shared_port && p->wifi == DEV_WIFI_BUILTIN,
	   "brick: its own model, Bluetooth, CPU shaders, no GPU frames, two speakers, no Player 2, built-in Wi-Fi");
	ck(p->screen_colors, "brick: Screen Colors, on its display's own gamma table");
	{
		/* Its core options: some, and none of the Pixel 2's rumble until a
		 * tester has felt its motor (src/device/brick_coreopts.c) */
		const dev_default *d = p->coreopts;
		bool any = false, rumble = false;

		for (; d && d->key; d++) {
			any = true;
			if (strstr(d->key, "rumble") || strstr(d->key, "pak1")) rumble = true;
		}
		ck(any && !rumble, "brick: its own core options, no rumble in them");
	}
	ck(menu && !strcmp(menu->label, "MENU") && menu->icon == DEV_ICON_NONE,
	   "brick: MENU is the word (no vendor mark as an icon)");
	ck(label_is(p, IN_BRIGHTDN, "F1") && label_is(p, IN_BRIGHTUP, "F2"),
	   "brick: F1 and F2 are the brightness keys");
}

/* The Smart Pro: the Brick's board with a 16:9 panel, brightness on
 * MENU+VOL, its own drawing */
static void smartpro(void)
{
	const device_profile *p = &device_profile_smartpro;
	const dev_button *menu = dev_button_of(p, IN_MENU);

	ck(!strcmp(p->id, "smartpro") && !strcmp(p->userdata, "smartpro") &&
	   !strcmp(p->boot_label, "SMARTBOOT") && strlen(p->boot_label) <= 11,
	   "smartpro: its id, folder and boot label (a FAT label, 11 at most)");
	ck(p->screen.panel_w == 720 && p->screen.panel_h == 1280 && p->screen.turn % 180 == 90 &&
	   p->screen.aspect_w == 16 && p->screen.aspect_h == 9,
	   "smartpro: a 720x1280 panel, turned a quarter, 16:9");
	ck(near(p->screen.active_w_mm, 109.8f) && near(p->screen.active_h_mm, 61.8f),
	   "smartpro: 109.8 x 61.8 mm of glass");
	ck(p->chords.bright == DEV_BRIGHT_MENU_VOL && p->chords.menu_on_release &&
	   !p->chords.lr2_taps && !p->chords.home_chord && !p->chords.battery_chord,
	   "smartpro: brightness on MENU+VOL, MENU on release, no taps, Home or battery lights");
	ck(p->model == &td_model_smartpro && !p->model_borrowed && p->bluetooth && !p->gpu_shaders && !p->gpu_frames &&
	   !p->speaker_mono && !p->player2 && !p->usb_shared_port && p->wifi == DEV_WIFI_BUILTIN,
	   "smartpro: its own drawing, the Brick's board's answers");
	ck(dev_button_of(p, IN_SELECT)->icon == DEV_ICON_SQUARE && dev_button_of(p, IN_START)->icon == DEV_ICON_PLAY,
	   "smartpro: SELECT's square and START's triangle, as printed");
	ck(p->coreopts == device_profile_brick.coreopts && p->screen_colors && !p->rumble,
	   "smartpro: the Brick's core options and Screen Colors, its motor not run yet");
	ck(menu && !strcmp(menu->label, "MENU") && menu->icon == DEV_ICON_NONE,
	   "smartpro: MENU is the word (no vendor mark as an icon)");
	ck(!dev_button_of(p, IN_BRIGHTUP) && !dev_button_of(p, IN_BRIGHTDN),
	   "smartpro: no brightness keys of its own");
	ck(label_is(p, IN_ACCEPT, "A") && label_is(p, IN_VOLDN, "VOL-") && label_is(p, IN_R2, "R2") &&
	   label_is(p, IN_SELECT, "SELECT") && label_is(p, IN_START, "START"),
	   "smartpro: A, VOL-, R2, SELECT and START as printed");
}

static void desktop(void)
{
	const device_profile *p = &device_profile_desktop, *b = &device_profile_brick;

	ck(!strcmp(p->id, "desktop") && !strcmp(p->userdata, "brick"),
	   "desktop: its id, and the Brick's folder the host has always used");
	ck(!memcmp(&p->chords, &b->chords, sizeof p->chords), "desktop: the Brick's chords");
	ck(p->model == &td_model_pixel2 && p->model_borrowed && p->bluetooth && p->gpu_shaders && p->gpu_frames &&
	   !p->speaker_mono && !p->player2 && !p->usb_shared_port && p->wifi == DEV_WIFI_BUILTIN,
	   "desktop: today's host answers (the Pixel 2's art, borrowed, shaders and GPU frames, the Brick's rest)");
}

/* The constant questions the launcher asks the platform, answered from the
 * profile in force */
static void answers(void)
{
	static const char *const ids[] = { "pixel2", "brick", "smartpro", "desktop" };
	char what[96];
	size_t i;

	for (i = 0; i < sizeof ids / sizeof ids[0]; i++) {
		const device_profile *p;

		ck(plat_profile_select(ids[i]), "a profile to answer from");
		p = plat_profile();
		snprintf(what, sizeof what, "%s: the platform answers from its profile", ids[i]);
		ck(plat_has_bluetooth() == p->bluetooth && plat_gpu_shaders() == p->gpu_shaders &&
		   plat_gpu_frames() == p->gpu_frames &&
		   plat_speaker_mono() == p->speaker_mono && plat_player2() == p->player2 &&
		   plat_brightness_on_volume() == (p->chords.bright == DEV_BRIGHT_MENU_VOL) &&
		   plat_battery_lights() == p->chords.battery_chord, what);
	}
	plat_profile_select("desktop");
}

/* Developer Mode's unlock: L2 and R2 held together, as taps where they are
 * taps (the Pixel 2, whose in_state never sees them held) and as buttons
 * where they are buttons; never one alone, never the other kind */
static void unlock(void)
{
	const dev_chords *px = &device_profile_pixel2.chords, *br = &device_profile_brick.chords;

	ck(dev_l2r2_held(px, true, true, false, false), "pixel2: both taps held unlock");
	ck(!dev_l2r2_held(px, true, false, false, false), "pixel2: one tap alone does not");
	ck(!dev_l2r2_held(px, false, false, true, true), "pixel2: the buttons' own state is not its");
	ck(dev_l2r2_held(br, false, false, true, true), "brick: both buttons held unlock");
	ck(!dev_l2r2_held(br, false, false, true, false), "brick: one button alone does not");
	ck(!dev_l2r2_held(br, true, true, false, false), "brick: taps it has none of do not");
}

/* The tutorial's handheld: each device's own beside its lessons, whatever
 * its chords; the desktop's borrowed one only with the Pixel 2's chords
 * (--fn-volume), not with the Brick's F1 and F2 it teaches */
static void tutorial(void)
{
	const device_profile *px = &device_profile_pixel2, *br = &device_profile_brick;
	const device_profile *dk = &device_profile_desktop;
	const dev_chords fn = dev_chords_fn_volume(dk->chords);

	ck(dev_tutorial_model(px, &px->chords) == &td_model_pixel2, "pixel2: the tutorial draws its model");
	ck(dev_tutorial_model(br, &br->chords) == &td_model_brick && dev_tutorial_model(br, &fn) == &td_model_brick,
	   "brick: the tutorial draws its own model, with its chords or the Pixel 2's");
	ck(!dev_tutorial_model(dk, &dk->chords), "desktop: none beside F1 and F2");
	ck(dev_tutorial_model(&device_profile_smartpro, &device_profile_smartpro.chords) == &td_model_smartpro,
	   "smartpro: the tutorial draws its own model, though its chords are the Pixel 2's");
	ck(dev_tutorial_model(dk, &fn) == &td_model_pixel2, "desktop: the Pixel 2's with --fn-volume");
}

/* Where a button is, for the design system: each model's places; none for
 * a key it has not */
static void places(void)
{
	const device_profile *px = &device_profile_pixel2, *br = &device_profile_brick;
	td_face f = TD_FACE_TOP;
	float x = 0.0f, z = 0.0f;

	ck(dev_button_place(px, IN_ACCEPT, &f, &x, &z) && f == TD_FACE_FRONT && x == 23.6f && z == -21.5f,
	   "pixel2: A on the front at (23.6, -21.5)");
	ck(dev_button_place(px, IN_MENU, &f, &x, &z) && f == TD_FACE_RIGHT && x == -1.54f && z == 17.13f,
	   "pixel2: MENU on the right side at (-1.54, 17.13)");
	ck(dev_button_place(px, IN_UP, &f, &x, &z) && f == TD_FACE_FRONT && x == -16.9f && z == -21.0f,
	   "pixel2: up is the d-pad's place");
	ck(dev_button_place(px, IN_L2, &f, &x, &z) && f == TD_FACE_TOP && x == -12.6f,
	   "pixel2: L2 on the top edge, left of the middle");
	f = TD_FACE_TOP; x = z = 7.0f;
	ck(!dev_button_place(px, IN_BRIGHTUP, &f, &x, &z) && f == TD_FACE_TOP && x == 7.0f && z == 7.0f,
	   "pixel2: no brightness key, and nothing set");
	ck(!dev_button_place(px, IN_NONE, &f, &x, &z) && !dev_button_place(px, IN_COUNT, &f, &x, &z),
	   "no place for no button");
	ck(dev_button_place(br, IN_ACCEPT, &f, &x, &z) && f == TD_FACE_FRONT && x == 27.3f && z == -17.15f,
	   "brick: A on the front at (27.3, -17.15)");
	ck(dev_button_place(br, IN_MENU, &f, &x, &z) && f == TD_FACE_FRONT && x == -10.5f && z == -46.45f,
	   "brick: MENU on the front, the bottom row's left");
	ck(dev_button_place(br, IN_BRIGHTUP, &f, &x, &z) && f == TD_FACE_FRONT && x == 5.0f,
	   "brick: brightness up is F2");
	ck(dev_button_place(br, IN_R1, &f, &x, &z) && f == TD_FACE_LEDGE && x == 27.85f,
	   "brick: R1 on the back ledge, right of the middle");
}

/* What the system has: the image's has line where it has one, whatever the
 * files say; the program's file on an image without one (none on the host) */
static void system_has(void)
{
	char dir[] = "/tmp/zyperos-sysfile-XXXXXX", path[64];
	FILE *f;

	if (!mkdtemp(dir)) { ck(0, "a scratch folder"); return; }
	snprintf(path, sizeof path, "%s/system", dir);
	setenv("ZYPEROS_SYSTEM_FILE", path, 1);
	if ((f = fopen(path, "w"))) {
		fputs("version 1.0.6\nupdate 1\ndevice pixel2\ntable mbr\npart-boot 1\nhas usbdrive hostapd\n", f);
		fclose(f);
	}
	ck(plat_system_has("usbdrive") && plat_system_has("hostapd"), "the image says usbdrive and hostapd: both");
	ck(!plat_system_has("teleporter") && !plat_system_has("") && !plat_system_has(NULL),
	   "and nothing it does not name");
	if ((f = fopen(path, "w"))) { fputs("version 1.0.6\ndevice brick\nhas\n", f); fclose(f); }
	ck(!plat_system_has("usbdrive") && !plat_system_has("hostapd"), "an empty has line: none");
	if ((f = fopen(path, "w"))) { fputs("version 1.0.5\nupdate 1\ndevice pixel2\nhashes x\n", f); fclose(f); }
	ck(!plat_system_has("usbdrive"), "an image from before the line: the files, none here");
	unlink(path);
	ck(!plat_system_has("usbdrive"), "no system file: the files, none here");
	unsetenv("ZYPEROS_SYSTEM_FILE");
	rmdir(dir);
}

/* Whether `z` is the two zone types `a` and `b`, in that order, and no more */
static bool zones_are(const char *const *z, const char *a, const char *b)
{
	return z && z[0] && !strcmp(z[0], a) && z[1] && !strcmp(z[1], b) && !z[2];
}

/* One thermal zone under `root`, as the kernel shows it: its folder, its
 * type and, unless NULL, its temperature in mC. Each path made is kept in
 * `made`, so the scratch folder goes again in the reverse order. */
static char made[48][128];
static int nmade;

static void made_dir(const char *path)
{
	if (nmade < (int)(sizeof made / sizeof made[0]) && mkdir(path, 0700) == 0)
		snprintf(made[nmade++], sizeof made[0], "%s", path);
}

static void made_file(const char *path, const char *text)
{
	FILE *f = fopen(path, "w");

	if (!f) return;
	fputs(text, f);
	fclose(f);
	if (nmade < (int)(sizeof made / sizeof made[0]))
		snprintf(made[nmade++], sizeof made[0], "%s", path);
}

static void zone(const char *root, int i, const char *type, const char *temp)
{
	char path[128];

	snprintf(path, sizeof path, "%s/sys", root);
	made_dir(path);
	snprintf(path, sizeof path, "%s/sys/class", root);
	made_dir(path);
	snprintf(path, sizeof path, "%s/sys/class/thermal", root);
	made_dir(path);
	snprintf(path, sizeof path, "%s/sys/class/thermal/thermal_zone%d", root, i);
	made_dir(path);
	snprintf(path, sizeof path, "%s/sys/class/thermal/thermal_zone%d/type", root, i);
	made_file(path, type);
	if (!temp) return;
	snprintf(path, sizeof path, "%s/sys/class/thermal/thermal_zone%d/temp", root, i);
	made_file(path, temp);
}

/* The chip's temperature (src/thermal.h): the zones a profile names, found by
 * their type wherever the kernel numbered them, the hottest of them in whole
 * degrees; a zone of another type, or with no reading, left out; -1 for none */
static void thermal(void)
{
	static const char *const battery[] = { "battery", NULL };
	static const char *const unknown[] = { "tsadc", "cpu-thermal", NULL };
	static const char *const none[] = { NULL };
	const char *const *px = device_profile_pixel2.thermal_zones;
	const char *const *br = device_profile_brick.thermal_zones;
	char root[] = "/tmp/zyperos-thermal-XXXXXX", other[] = "/tmp/zyperos-thermal-XXXXXX";

	ck(zones_are(px, "soc-thermal", "gpu-thermal"),
	   "pixel2: its chip's zones, px30.dtsi's soc-thermal and gpu-thermal");
	ck(zones_are(br, "cpu_thermal_zone", "gpu_thermal_zone"),
	   "brick: its chip's zones, its device tree's cpu_thermal_zone and gpu_thermal_zone");
	ck(!device_profile_desktop.thermal_zones, "desktop: no zones, the computer's chip is not watched");
	if (!mkdtemp(root) || !mkdtemp(other)) { ck(0, "two scratch folders"); return; }

	/* Both devices' zones on one sysfs, numbered with a gap, a hotter zone
	 * of a type neither names among them */
	zone(root, 0, "soc-thermal\n", "61500\n");
	zone(root, 1, "gpu-thermal\n", "72900\n");
	zone(root, 2, "battery\n", "99000\n");
	zone(root, 4, "cpu_thermal_zone\n", "48000\n");
	zone(root, 5, "gpu_thermal_zone\n", "51999\n");
	zone(root, 6, "ddr_thermal_zone\n", "90000\n");
	ck(thermal_max_c(px, root) == 72, "pixel2: the hotter of its two zones, in whole degrees");
	ck(thermal_max_c(br, root) == 51, "brick: the hotter of its two, past a gap in the numbers, not the DDR's");
	ck(thermal_max_c(battery, root) == 99, "a zone by its type alone");
	ck(thermal_max_c(unknown, root) == -1, "types no zone has: -1");
	ck(thermal_max_c(none, root) == -1 && thermal_max_c(NULL, root) == -1, "no types: -1");

	/* A zone with no temperature, and one under 0 C, are no reading */
	zone(other, 0, "soc-thermal\n", NULL);
	zone(other, 1, "gpu-thermal\n", "40000\n");
	zone(other, 2, "cpu_thermal_zone\n", "-5000\n");
	ck(thermal_max_c(px, other) == 40, "pixel2: a zone with no reading left out");
	ck(thermal_max_c(br, other) == -1, "brick: one zone under 0 C and the other not there: -1");

	while (nmade > 0) remove(made[--nmade]);
	rmdir(other);
	rmdir(root);
	ck(thermal_max_c(px, root) == -1, "no thermal zones at all: -1");
}

/* Each device's build and card (src/device/build.h): its own id, the
 * shared aarch64 baseline, its Diatom port, its partitions and helpers */
static void builds(void)
{
	const dev_build *px = &device_build_pixel2, *br = &device_build_brick;

	ck(!strcmp(px->id, device_profile_pixel2.id) && !strcmp(br->id, device_profile_brick.id),
	   "each build is its profile's device");
	ck(!strcmp(px->arch, "aarch64") && !strcmp(px->mcpu, "cortex-a53") && !strcmp(px->cpu, "cortex-a35"),
	   "pixel2: aarch64, built for the A53 baseline, an A35 inside");
	ck(!strcmp(br->arch, "aarch64") && !strcmp(br->mcpu, "cortex-a53") && !strcmp(br->cpu, "cortex-a53"),
	   "brick: aarch64, the same baseline, an A53 inside");
	ck(!strcmp(px->diatom_port, "pixel2") && !strcmp(br->diatom_port, "brick"), "each its own Diatom port");
	ck(!strcmp(px->table, "mbr") && px->part_boot == 1 && px->part_root_a == 2 && px->part_root_b == 4 &&
	   px->part_games == 3, "pixel2: MBR, boot 1, roots 2 and 4, games 3");
	ck(!strcmp(br->table, "gpt") && br->part_boot == 1 && br->part_root_a == 2 && br->part_root_b == 4 &&
	   br->part_games == 5, "brick: GPT, boot 1, roots 2 and 4 (the Pixel 2's numbers), state 3, games 5");
	ck(px->helpers == (DEV_HELPER_PORTKEYS | DEV_HELPER_ZYHOME | DEV_HELPER_UPDATE | DEV_HELPER_P2D |
	                   DEV_HELPER_PORTS), "pixel2: portkeys, zyhome, System Update, Player 2, ports");
	ck(br->helpers == (DEV_HELPER_UPDATE | DEV_HELPER_BTPLAYER | DEV_HELPER_BTALSA),
	   "brick: System Update, btplayer and Bluetooth sound");
	ck(!strcmp(px->backend, "pixel2") && !strcmp(br->backend, "brick"), "pixel2 and brick: each its own platform layer");

	/* The Smart Pro: the Brick's chip, platform layer, Diatom port and card
	 * layout, and sppad for its serial halves */
	const dev_build *sp = &device_build_smartpro;
	ck(!strcmp(sp->id, device_profile_smartpro.id), "smartpro: the build is its profile's device");
	ck(!strcmp(sp->arch, "aarch64") && !strcmp(sp->mcpu, "cortex-a53") && !strcmp(sp->cpu, "cortex-a53"),
	   "smartpro: aarch64, the A53 baseline, an A53 inside");
	ck(!strcmp(sp->backend, "brick") && !strcmp(sp->diatom_port, "brick"),
	   "smartpro: the Brick's platform layer and Diatom port");
	ck(!strcmp(sp->table, "gpt") && sp->part_boot == 1 && sp->part_root_a == 2 && sp->part_root_b == 4 &&
	   sp->part_games == 5, "smartpro: the Brick's card layout");
	ck(sp->helpers == (DEV_HELPER_UPDATE | DEV_HELPER_BTPLAYER | DEV_HELPER_BTALSA | DEV_HELPER_SPPAD),
	   "smartpro: the Brick's helpers, and sppad");

	/* A device's card folder is its id: mk/cross.mk names the build's
	 * .userdata folder by it (ZYPEROS_DEVICE_ID) for a platform layer two
	 * devices share */
	ck(!strcmp(device_profile_pixel2.userdata, px->id) && !strcmp(device_profile_brick.userdata, br->id) &&
	   !strcmp(device_profile_smartpro.userdata, sp->id), "each device's .userdata folder is its id");
}

/* A button as `p` prints it (src/ds/button.h): its glyph's shape, mark and
 * word */
static bool key_is(const device_profile *p, const char *name, ds_key_shape shape, ds_key_mark mark,
                   const char *text)
{
	ds_key k;

	ds_keys_set(p ? p->buttons : NULL, p ? p->n_buttons : 0);
	k = ds_key_of(name);
	ds_keys_set(NULL, 0);
	return k.shape == shape && k.mark == mark && !strcmp(k.text, text);
}

/* The buttons' look: the Pixel 2's as its keys are printed, the host's as
 * the Pixel 2's (its shots always showed them), the Brick's own */
static void menu_look(void)
{
	const device_profile *px = &device_profile_pixel2, *br = &device_profile_brick,
	                     *dt = &device_profile_desktop;

	ck(key_is(px, "MENU", DS_KEY_FACE, DS_KEY_MARK_DOTS, "MENU"), "pixel2: MENU is its three-dot key");
	ck(key_is(px, "SELECT", DS_KEY_FACE, DS_KEY_MARK_NONE, "SE") &&
	   key_is(px, "START", DS_KEY_FACE, DS_KEY_MARK_NONE, "ST"),
	   "pixel2: SELECT and START are round keys printed SE and ST (tools/build-pixel2-model.py)");
	ck(key_is(px, "A", DS_KEY_FACE, DS_KEY_MARK_NONE, "A") && key_is(px, "L1", DS_KEY_PILL, DS_KEY_MARK_NONE, "L1"),
	   "pixel2: A is a face, L1 a pill");
	ck(key_is(dt, "MENU", DS_KEY_FACE, DS_KEY_MARK_DOTS, "MENU") &&
	   key_is(dt, "SELECT", DS_KEY_FACE, DS_KEY_MARK_NONE, "SE") &&
	   key_is(dt, "START", DS_KEY_FACE, DS_KEY_MARK_NONE, "ST"),
	   "desktop: the Pixel 2's MENU, SELECT and START");
	ck(key_is(dt, "F1", DS_KEY_PILL, DS_KEY_MARK_NONE, "F1"), "desktop: the Brick's F1, a pill");
	ck(key_is(br, "MENU", DS_KEY_PILL, DS_KEY_MARK_NONE, "MENU"), "brick: MENU is the word on its key");
	ck(key_is(br, "SELECT", DS_KEY_FACE, DS_KEY_MARK_SQUARE, "SELECT"), "brick: SELECT is round, a square on it");
	ck(key_is(br, "START", DS_KEY_FACE, DS_KEY_MARK_PLAY, "START"), "brick: START is round, a play triangle on it");
	ck(key_is(br, "POWER", DS_KEY_PILL, DS_KEY_MARK_NONE, "POWER"), "brick: POWER's word in a pill");
	ck(key_is(br, "F2", DS_KEY_PILL, DS_KEY_MARK_NONE, "F2"), "brick: F2 a pill");
	ck(key_is(&device_profile_smartpro, "MENU", DS_KEY_PILL, DS_KEY_MARK_NONE, "MENU") &&
	   key_is(&device_profile_smartpro, "SELECT", DS_KEY_FACE, DS_KEY_MARK_SQUARE, "SELECT") &&
	   key_is(&device_profile_smartpro, "START", DS_KEY_FACE, DS_KEY_MARK_PLAY, "START") &&
	   key_is(&device_profile_smartpro, "A", DS_KEY_FACE, DS_KEY_MARK_NONE, "A"),
	   "smartpro: MENU its word in a pill, SELECT's square and START's triangle as the Brick's, A a face");
	ck(key_is(NULL, "MENU", DS_KEY_FACE, DS_KEY_MARK_DOTS, "MENU") &&
	   key_is(NULL, "SELECT", DS_KEY_PILL, DS_KEY_MARK_NONE, "SELECT") &&
	   key_is(NULL, "Y", DS_KEY_FACE, DS_KEY_MARK_NONE, "Y"), "no table: the glyphs the launcher always drew");
	ck(key_is(br, "< >", DS_KEY_DPAD_LR, DS_KEY_MARK_NONE, "< >") &&
	   key_is(px, "^ v", DS_KEY_DPAD_UD, DS_KEY_MARK_NONE, "^ v"), "the d-pad's hints are the d-pad");
	ck(key_is(br, "PIN", DS_KEY_PILL, DS_KEY_MARK_NONE, "PIN"), "a name no button has: a pill with its word");
	{
		/* A stick's cap, for the handhelds that have them (the Smart Pro's
		 * and the Brick Pro's L3 and R3), and a round key with a word */
		static const dev_button sticks[] = {
			{ IN_L1, "L3", DEV_CAP_STICK, DEV_ICON_NONE, 0 },
			{ IN_R1, "R3", DEV_CAP_ROUND, DEV_ICON_STICK, 0 },
			{ IN_START, "HOME", DEV_CAP_ROUND, DEV_ICON_NONE, 0 },
		};
		ds_key k;

		ds_keys_set(sticks, 3);
		k = ds_key_of("L1");
		ck(k.shape == DS_KEY_STICK && !strcmp(k.text, "L3"), "a stick's cap is the stick, its word on it");
		k = ds_key_of("R1");
		ck(k.shape == DS_KEY_STICK && !strcmp(k.text, "R3"), "a stick mark makes the stick too");
		k = ds_key_of("START");
		ck(k.shape == DS_KEY_PILL && !strcmp(k.text, "HOME"), "a round key with a word is a pill");
		ds_keys_set(NULL, 0);
	}
	ck(ds_key_id("F1") == IN_BRIGHTDN && ds_key_id("F2") == IN_BRIGHTUP && ds_key_id("PIN") == IN_NONE,
	   "the ids' names");
	/* A chord by what it does: each handheld's own buttons */
	ck(!strcmp(dev_role_keys(&device_profile_pixel2.chords, DEV_ROLE_BRIGHTNESS), "MENU+VOL") &&
	   !strcmp(dev_role_keys(&device_profile_brick.chords, DEV_ROLE_BRIGHTNESS), "F1/F2") &&
	   !strcmp(dev_role_keys(&device_profile_desktop.chords, DEV_ROLE_BRIGHTNESS), "F1/F2") &&
	   !strcmp(dev_role_keys(&device_profile_smartpro.chords, DEV_ROLE_BRIGHTNESS), "MENU+VOL"),
	   "brightness: MENU+VOL on the Pixel 2 and the Smart Pro, F1/F2 on the Brick and the host");
	ck(!dev_role_keys(&device_profile_smartpro.chords, DEV_ROLE_HOME) &&
	   !dev_role_keys(&device_profile_smartpro.chords, DEV_ROLE_BATTERY),
	   "smartpro: no Home chord (no zyhome on its board) and no battery lights");
	ck(!strcmp(dev_role_keys(&device_profile_pixel2.chords, DEV_ROLE_HOME), "MENU+START") &&
	   !dev_role_keys(&device_profile_brick.chords, DEV_ROLE_HOME), "home: MENU+START on the Pixel 2 alone");
	ck(!strcmp(dev_role_keys(&device_profile_pixel2.chords, DEV_ROLE_BATTERY), "L2+R2") &&
	   !dev_role_keys(&device_profile_brick.chords, DEV_ROLE_BATTERY), "battery: L2+R2 where it has lights");
	ck(!dev_role_keys(NULL, DEV_ROLE_BRIGHTNESS), "no chords: no buttons");
	/* About's kernel thanks: ROCKNIX's on the Pixel 2 and on the host's shots,
	 * Allwinner's on the Brick */
	ck(!strcmp(device_profile_pixel2.kernel.by, "ROCKNIX") &&
	   !strcmp(device_profile_pixel2.kernel.of, "Pixel 2"), "pixel2: ROCKNIX's kernel");
	ck(!strcmp(device_profile_desktop.kernel.by, "ROCKNIX"), "desktop: the Pixel 2's kernel thanks");
	ck(!strcmp(device_profile_brick.kernel.by, "Allwinner") &&
	   !strcmp(device_profile_brick.kernel.of, "Brick"), "brick: Allwinner's kernel");
	ck(!strcmp(device_profile_smartpro.kernel.by, "Allwinner") &&
	   !strcmp(device_profile_smartpro.kernel.of, "Smart Pro"), "smartpro: Allwinner's kernel");
}

static void choosing(void)
{
	ck(plat_profile() == &device_profile_desktop, "the host starts as desktop");
	ck(plat_profile_select("pixel2") && plat_profile() == &device_profile_pixel2,
	   "--device pixel2 takes the Pixel 2's profile");
	ck(!plat_profile_select("gameboy") && plat_profile() == &device_profile_pixel2,
	   "an unknown id is refused and changes nothing");
	ck(plat_profile_select("brick") && plat_profile() == &device_profile_brick,
	   "--device brick takes the Brick's");
	ck(plat_profile_select("smartpro") && plat_profile() == &device_profile_smartpro,
	   "--device smartpro takes the Smart Pro's");
	ck(plat_profile_select("desktop") && plat_profile() == &device_profile_desktop, "and back");
	ck(!dev_button_of(plat_profile(), IN_NONE), "no button for IN_NONE");
	ck(!plat_system_has("teleporter") && !plat_system_has(NULL),
	   "a part the system cannot have is never had");
	{
		char ids[64];

		plat_profile_ids(ids, sizeof ids);
		ck(!strcmp(ids, "pixel2, brick, smartpro, desktop"), "the linked ids, for --device's error");
		plat_profile_ids(ids, 9);
		ck(!strcmp(ids, "pixel2, "), "and cut to the room there is");
	}
}

/* The canvas (src/canvas.h): 768 high, as wide as the panel's shape, on the
 * 8-unit grid; a 4:3 handheld's is the 1024 every screen was made in */
static void canvas(void)
{
	static const struct { int aw, ah, w; } shapes[] = {
		{ 4, 3, 1024 }, { 16, 9, 1368 }, { 3, 2, 1152 }, { 1, 1, 768 }, { 5, 3, 1280 },
		{ 16, 10, 1232 }, { 0, 0, 1024 }, { 4, 0, 1024 }, { 1, 4, CANVAS_W_MIN }, { 4, 1, CANVAS_W_MAX },
	};
	size_t i;
	char what[96];

	for (i = 0; i < sizeof shapes / sizeof shapes[0]; i++) {
		int w = canvas_w_for(shapes[i].aw, shapes[i].ah);

		snprintf(what, sizeof what, "canvas: %d:%d is %d units wide (got %d)", shapes[i].aw, shapes[i].ah,
		         shapes[i].w, w);
		ck(w == shapes[i].w && w % 8 == 0, what);
	}
	canvas_init(&device_profile_pixel2);
	ck(canvas_w == 1024 && !canvas_wide() && canvas_col_x() == 0, "canvas: the Pixel 2's is 1024, as it was");
	canvas_init(&device_profile_brick);
	ck(canvas_w == 1024 && !canvas_wide(), "canvas: the Brick's is 1024, as it was");
	canvas_init(&device_profile_desktop);
	ck(canvas_w == 1024, "canvas: the host's desktop is 1024");
	canvas_init(&device_profile_smartpro);
	ck(canvas_w == 1368 && canvas_wide() && canvas_col_x() == 172, "canvas: the Smart Pro's is 1368, its 4:3 column at 172");
	canvas_init(NULL);
	ck(canvas_w == 1024, "canvas: no profile is 4:3");
}

int main(void)
{
	whole(&device_profile_pixel2);
	whole(&device_profile_brick);
	whole(&device_profile_smartpro);
	whole(&device_profile_desktop);
	pixel2();
	brick();
	smartpro();
	desktop();
	answers();
	unlock();
	tutorial();
	places();
	system_has();
	thermal();
	builds();
	menu_look();
	choosing();
	canvas();
	if (fails) {
		printf("devices: %d of %d checks failed\n", fails, checks);
		return 1;
	}
	printf("devices: %d checks passed\n", checks);
	return 0;
}
