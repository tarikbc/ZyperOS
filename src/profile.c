/* SPDX-License-Identifier: GPL-3.0-only */
/* Which device profile is in force (src/device/profile.h). The build names
 * its own: mk/cross.mk the device it builds for (ZYPEROS_PROFILE is
 * device_profile_pixel2 or device_profile_brick), mk/native.mk the host's
 * desktop, with every profile linked (ZYPEROS_PROFILES_ALL) so --device can
 * wear any of them. Plain C, no SDL: the checks link it as it is. */
#include "device/profile.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef ZYPEROS_PROFILE
#error "ZYPEROS_PROFILE names the build's device profile (mk/cross.mk, mk/native.mk)"
#endif

static const device_profile *const linked[] = {
#ifdef ZYPEROS_PROFILES_ALL
	&device_profile_pixel2, &device_profile_brick, &device_profile_smartpro, &device_profile_r36s, &device_profile_desktop,
#else
	&ZYPEROS_PROFILE,
#endif
};

static const device_profile *active = &ZYPEROS_PROFILE;

const device_profile *plat_profile(void) { return active; }

bool plat_profile_select(const char *id)
{
	size_t i;

	for (i = 0; id && i < sizeof linked / sizeof linked[0]; i++)
		if (!strcmp(linked[i]->id, id)) {
			active = linked[i];
			return true;
		}
	return false;
}

const device_profile *plat_profile_at(int i)
{
	return i >= 0 && (size_t)i < sizeof linked / sizeof linked[0] ? linked[i] : NULL;
}

void plat_profile_ids(char *out, size_t n)
{
	size_t i, at = 0;

	if (!n) return;
	out[0] = 0;
	for (i = 0; i < sizeof linked / sizeof linked[0] && at < n; i++) {
		int w = snprintf(out + at, n - at, "%s%s", i ? ", " : "", linked[i]->id);

		if (w < 0) break;
		at += (size_t)w;
	}
}

bool plat_has_bluetooth(void)        { return active->bluetooth; }
bool plat_gpu_shaders(void)          { return active->gpu_shaders; }
bool plat_gpu_frames(void)           { return active->gpu_frames; }
bool plat_speaker_mono(void)         { return active->speaker_mono; }
bool plat_has_mute_switch(void)      { return active->mute_switch; }
bool plat_has_rumble(void)           { return active->rumble; }
bool plat_brightness_on_volume(void) { return active->chords.bright == DEV_BRIGHT_MENU_VOL; }
bool plat_battery_lights(void)       { return active->chords.battery_chord; }
bool plat_player2(void)              { return active->player2; }
bool plat_screen_colors_here(void)   { return active->screen_colors; }
bool plat_night_here(void)           { return active->screen_colors; }

/* The image's "has" line (/etc/zyperos/system, written by plastron at the
 * image's build: "has usbdrive hostapd"): 1 when it names `part`, 0 when it
 * does not, -1 where the file has no such line (an image from before it).
 * ZYPEROS_SYSTEM_FILE stands for the file in the checks. */
static int system_has_line(const char *part)
{
	const char *path = getenv("ZYPEROS_SYSTEM_FILE");
	char line[256];
	int said = -1;
	FILE *f = fopen(path ? path : "/etc/zyperos/system", "r");

	if (!f) return -1;
	while (said < 0 && fgets(line, sizeof line, f)) {
		char *w;

		line[strcspn(line, "\r\n")] = 0;
		if (strncmp(line, "has", 3) || (line[3] && line[3] != ' ')) continue;
		said = 0;
		for (w = strtok(line + 3, " "); w; w = strtok(NULL, " "))
			if (!strcmp(w, part)) said = 1;
	}
	fclose(f);
	return said;
}

bool plat_system_has(const char *part)
{
	static const struct { const char *part, *path; } parts[] = {
		{ "usbdrive", "/usr/sbin/usbdrive" },
		{ "hostapd",  "/usr/sbin/hostapd" },
	};
	size_t i;
	int said;

	if (!part || !*part) return false;
	if ((said = system_has_line(part)) >= 0) return said == 1;
	/* An image from before its has line: the program's file, as then */
	for (i = 0; i < sizeof parts / sizeof parts[0]; i++)
		if (!strcmp(parts[i].part, part)) return access(parts[i].path, X_OK) == 0;
	return false;
}

dev_chords dev_chords_fn_volume(dev_chords base)
{
	base.bright = DEV_BRIGHT_MENU_VOL;
	base.menu_on_release = true;
	base.home_chord = true;
	return base;
}

const td_model *dev_tutorial_model(const device_profile *p, const dev_chords *c)
{
	if (p->model_borrowed) return c->bright == DEV_BRIGHT_MENU_VOL ? p->model : NULL;
	return p->model;
}

bool dev_l2r2_held(const dev_chords *c, bool tap_l2, bool tap_r2, bool down_l2, bool down_r2)
{
	return c->lr2_taps ? tap_l2 && tap_r2 : down_l2 && down_r2;
}

const dev_button *dev_button_of(const device_profile *p, in_button b)
{
	int i;

	for (i = 0; p && i < p->n_buttons; i++)
		if (p->buttons[i].id == b) return &p->buttons[i];
	return NULL;
}

bool dev_button_place(const device_profile *p, in_button b, td_face *face, float *x, float *z)
{
	/* The key of the handheld each button is */
	static const td_key KEY[IN_COUNT] = {
		[IN_LEFT] = TD_DPAD, [IN_RIGHT] = TD_DPAD, [IN_UP] = TD_DPAD, [IN_DOWN] = TD_DPAD,
		[IN_ACCEPT] = TD_A, [IN_BACK] = TD_B, [IN_X] = TD_X, [IN_Y] = TD_Y,
		[IN_L1] = TD_L1, [IN_R1] = TD_R1, [IN_START] = TD_START, [IN_SELECT] = TD_SELECT,
		[IN_MENU] = TD_MENU, [IN_VOLUP] = TD_VOLUP, [IN_VOLDN] = TD_VOLDN,
		[IN_BRIGHTUP] = TD_F2, [IN_BRIGHTDN] = TD_F1, [IN_POWER] = TD_POWER,
		[IN_L2] = TD_L2, [IN_R2] = TD_R2, [IN_L3] = TD_L3, [IN_R3] = TD_R3 };
	const td_part *part;

	if (!p || !p->model || b < 0 || b >= IN_COUNT) return false;
	part = td_part_of(p->model, KEY[b]);
	if (!part) return false;
	*face = part->face;
	*x = part->x;
	*z = part->z;
	return true;
}
