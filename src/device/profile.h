/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_DEVICE_PROFILE_H
#define ZYPEROS_DEVICE_PROFILE_H

/* What a handheld is, stated once (docs/building.md,
 * Devices): its name and folders, its
 * screen as glass and pixels, its controls as they are printed, the chords it
 * needs because a button is missing or doubles as another, and what it has -
 * Bluetooth, a speaker or two, a GPU that draws the old screens. Facts, never
 * guesses: everything here was true of the device when it was written, and
 * nothing is worked out at run time.
 *
 * Live questions stay in the device files (src/device/<name>.c): whether a
 * Wi-Fi adapter is plugged in, whether the charger's limit can be written now,
 * whether MENU is being held. A screen asks the profile what the handheld has
 * and the platform what it is doing.
 *
 * One profile a device, in src/device/<id>_profile.c, plain data with no SDL
 * so a check links it as it is. The device build links its own; the host
 * build links them all and wears one (--device), so every handheld's screens
 * can be looked at on a computer. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../inbutton.h"
#include "td_model.h"   /* td_model: a handheld's 3D drawing */

/* How brightness is changed: keys of its own (the Brick's F1 and F2), or
 * MENU with a volume key, on a device that has none (the Pixel 2) */
typedef enum { DEV_BRIGHT_KEYS, DEV_BRIGHT_MENU_VOL } dev_bright;

/* What a missing or doubled button makes the player press instead. */
typedef struct {
	dev_bright bright;
	/* MENU is FUNCTION, seen when it is let go (a chord's press may be next) */
	bool menu_on_release;
	/* L2 and R2 are taps, seen when let go without the other: together they
	 * are the battery's level on the lights */
	bool lr2_taps;
	/* MENU+START held a second goes Home from anything (tools/zyhome.c) */
	bool home_chord;
	/* L2+R2 flashes the battery's level on lights of the device's own */
	bool battery_chord;
} dev_chords;

/* A control's cap as the device has it, and the mark on it from the design
 * system's closed set (never a vendor's logo: a key that shows one is its
 * word, as the Brick's MENU). The glyph on screen is drawn from these in the
 * theme's colors; cap_rgb is for the 3D handheld alone. */
typedef enum { DEV_CAP_ROUND, DEV_CAP_PILL, DEV_CAP_ROCKER, DEV_CAP_DPAD,
               DEV_CAP_SHOULDER, DEV_CAP_STICK } dev_cap;
typedef enum { DEV_ICON_NONE, DEV_ICON_SQUARE, DEV_ICON_PLAY, DEV_ICON_DOTS,
               DEV_ICON_STICK } dev_icon;

typedef struct {
	in_button   id;
	const char *label;       /* as printed: "A", "MENU", "F1", "VOL+" */
	dev_cap     cap;
	dev_icon    icon;
	uint32_t    cap_rgb;     /* 0xRRGGBB, 0 for the body's own dark */
} dev_button;

/* A setting's default a device gives, a library key and its value: a core
 * option tuned on it ("coreopt.<tag>.<option>", src/coreopts.h) */
typedef struct { const char *key, *value; } dev_default;

typedef enum { DEV_WIFI_NONE, DEV_WIFI_ADAPTER, DEV_WIFI_BUILTIN } dev_wifi;

typedef struct device_profile {
	const char *id;          /* "pixel2": --device, the build's DEVICE */
	const char *name;        /* "GKD Pixel 2", as people call it */
	/* About's thanks for its kernel: who made it ("ROCKNIX"), and the short
	 * name it is "the %s kernel" of ("Pixel 2"), in the thanks' own words */
	struct { const char *by, *of; } kernel;
	const char *userdata;    /* the card's .userdata/<this> folder */
	const char *boot_label;  /* the boot partition's FAT label */
	/* The glass and its pixels: physical facts only. The layout system
	 * works the canvas out from them, the platform the scale. */
	struct {
		int   panel_w, panel_h;          /* pixels, as the panel scans */
		int   turn;                      /* clockwise degrees to the picture */
		float active_w_mm, active_h_mm;  /* the lit area, landscape */
		int   inset_t, inset_r, inset_b, inset_l;   /* panel pixels */
		int   aspect_w, aspect_h;
		bool  still;                     /* a panel still can be saved */
	} screen;
	dev_chords        chords;
	const dev_button *buttons;
	int               n_buttons;
	/* Its 3D drawing for the tutorial and the Controls tab (td_model.h),
	 * NULL where it has none yet: never another device's, but on the host */
	const td_model *model;
	/* `model` is another device's: the host's desktop borrows the Pixel 2's
	 * for the Controls tab while it teaches the Brick's F1 and F2, so the
	 * tutorial shows it only with the Pixel 2's chords (dev_tutorial_model) */
	bool model_borrowed;
	bool bluetooth;
	bool gpu_shaders;        /* Diatom's old screens are a GPU's pass here */
	/* Diatom gives a core that asks for one a GPU to draw its frames on
	 * (SET_HW_RENDER): the Dreamcast's, the N64's, the PSP's, Saturn's */
	bool gpu_frames;
	bool speaker_mono;       /* one built-in speaker */
	/* A rumble motor the launcher and Diatom run (Diatom's caps.rumble):
	 * MENU > Sound > Rumble and the in-game Controls tab's row are here
	 * alone, and the step's buzz (plat_rumble_buzz) */
	bool rumble;
	bool player2;            /* NES Player 2 was built and tried on it */
	bool usb_shared_port;    /* one port for the computer and accessories */
	/* A gamma table between the frame and the panel, per channel, that
	 * MENU > Display > Screen Colors sets (src/panelgamma.h): the Pixel 2's VOP */
	bool screen_colors;
	dev_wifi wifi;
	/* The chip's thermal zones by their type, as its device tree names
	 * them, the hottest of them its temperature (src/thermal.h);
	 * NULL-ended, or NULL where none is read */
	const char *const *thermal_zones;
	/* The core options measured on this handheld, its GPU or its motor,
	 * given at run time under the player's own (src/coreopts.h); ends in
	 * { NULL, NULL }, or NULL for none */
	const dev_default *coreopts;
} device_profile;

extern const device_profile device_profile_pixel2;
extern const device_profile device_profile_brick;
extern const device_profile device_profile_smartpro;
extern const device_profile device_profile_desktop;

/* The Pixel 2's core options (src/device/pixel2_coreopts.c), linked on every
 * device: they are what 1.0.5 seeded into every card's library, which the
 * one-time cleanup (coreopts_migrate) takes out wherever the card is now */
extern const dev_default dev_coreopts_pixel2[];

/* The profile in force, never NULL: the build's own, or the one the host
 * build was told to wear */
const device_profile *plat_profile(void);
/* Wear the linked profile named `id`; false, and nothing changed, when the
 * build has none by that name (a device build has its own alone) */
bool plat_profile_select(const char *id);
/* The `i`th linked profile, from 0, or NULL past the last: every handheld on
 * the host, the device's own alone on a device */
const device_profile *plat_profile_at(int i);
/* The linked profiles' ids, ", " between them, into `out` (for --device's
 * error: "pixel2, brick, desktop" on the host, the device's own on a device) */
void plat_profile_ids(char *out, size_t n);
/* `b` as `p` prints it, or NULL where `p` has no such control */
const dev_button *dev_button_of(const device_profile *p, in_button b);
/* What a chord does, named rather than spelled: screens ask for the role and
 * get the buttons this handheld does it with */
typedef enum {
	DEV_ROLE_BRIGHTNESS,  /* MENU+VOL on the Pixel 2, F1/F2 where they are keys */
	DEV_ROLE_HOME,        /* MENU+START held, where zyhome runs */
	DEV_ROLE_BATTERY      /* L2+R2, the battery's level on lights of its own */
} dev_role;
/* The buttons `c` does `role` with, as ids' names with "+" for together and
 * "/" for either ("MENU+VOL", "F1/F2", src/ds/button.h); NULL where the
 * handheld has no such chord. Inline, so a check that links the Controls
 * pages needs no profile. */
static inline const char *dev_role_keys(const dev_chords *c, dev_role role)
{
	if (!c) return (const char *)0;
	switch (role) {
	/* The Pixel 2's by the names its glyphs carry, MENU and VOL, as the
	 * tutorial teaches it (DESIGN.md 12: chords with a plus) */
	case DEV_ROLE_BRIGHTNESS: return c->bright == DEV_BRIGHT_MENU_VOL ? "MENU+VOL" : "F1/F2";
	case DEV_ROLE_HOME:       return c->home_chord ? "MENU+START" : (const char *)0;
	case DEV_ROLE_BATTERY:    return c->battery_chord ? "L2+R2" : (const char *)0;
	}
	return (const char *)0;
}
/* Where `b` is on `p`'s handheld, as its model has it (td_model.h: the face
 * in *face, (x, z) on it in *x and *z): the d-pad for its four directions,
 * F1 and F2 for brightness down and up. False, and nothing set, where `p`
 * has no model or its model has no such key. For the design system, which
 * points at a key where it is. */
bool dev_button_place(const device_profile *p, in_button b, td_face *face, float *x, float *z);
/* `base` with the Pixel 2's brightness chord, MENU on release and Home: the
 * shots' --fn-volume, which shows those screens on the host as the Pixel 2
 * has them until the shots wear --device pixel2 (step 5 of the device spec) */
dev_chords dev_chords_fn_volume(dev_chords base);
/* The handheld the tutorial draws beside its lessons for `p` with chords
 * `c`, NULL for none: `p`'s own model always; a borrowed one (model_borrowed,
 * the host's desktop's) only where brightness is MENU+VOL, the chords of the
 * device it is (--fn-volume), not beside the F1 and F2 it would not show. */
const td_model *dev_tutorial_model(const device_profile *p, const dev_chords *c);
/* L2 and R2 both held this moment, for Developer Mode's unlock: as taps where
 * the chords say they are taps (`tap_l2`, `tap_r2`: plat_tap_held), as the
 * buttons' own state everywhere else (`down_l2`, `down_r2`: in_state) */
bool dev_l2r2_held(const dev_chords *c, bool tap_l2, bool tap_r2, bool down_l2, bool down_r2);

/* The questions the launcher asks the platform whose answers are facts about
 * the handheld, from the profile in force (src/profile.c). Live questions
 * (plat_has_wifi: an adapter plugged in; plat_menu_held, plat_tap_held) stay
 * in src/platform.h and the device files. */
/* Bluetooth at all, not whether it is on: a device without it hides
 * everything that needs it (src/sys_menu.h) */
bool plat_has_bluetooth(void);
/* Whether Diatom draws its old screens here, the shaders a GPU draws (the
 * CRT, scanlines, the LCD grid; src/app.h SHADERS' gpu): on the Pixel 2,
 * whose pass is a GPU's; not on the Brick, whose is the CPU's */
bool plat_gpu_shaders(void);
/* Whether a game whose core draws with the GPU can run here (the profile's
 * gpu_frames): the systems that need it (config/systems.cfg's needs lines)
 * have no shelf where it cannot, and their games say why they do not start */
bool plat_gpu_frames(void);
/* Whether the built-in speaker is one speaker, as on the GKD Pixel 2, where
 * the Brick has two: Audio Channels' Auto is mono there and stereo on
 * everything else (src/audioout.h). */
bool plat_speaker_mono(void);
/* Whether the handheld has a rumble motor ZyperOS runs (the profile's
 * rumble): the Pixel 2's, on PWM0 through the joypad's force feedback. Not
 * the Brick's yet: its motor (GPIO 227 on the bldo3 rail) waits for the
 * tester's buzz (the Brick parity plan's item 14). */
bool plat_has_rumble(void);
/* Whether brightness is FUNCTION with the volume keys, the device having no
 * brightness keys of its own. What MENU > Controls calls them (controls.h). */
bool plat_brightness_on_volume(void);
/* Whether the device shows the battery on lights of its own, L2 with R2
 * flashing the level there outside a game (the Pixel 2's four side lights,
 * which plastron's gauge drives). What MENU > Controls says. */
bool plat_battery_lights(void);
/* Whether NES Player 2 (src/p2.h) is offered here: a device whose own
 * hotspot (a USB Wi-Fi adapter, hostapd) and cable link (the USB network
 * gadget) it was built and tried on, the Pixel 2. Nothing in a game's menu
 * says it elsewhere. */
bool plat_player2(void);
/* Whether MENU > Display offers Screen Colors (the profile's screen_colors):
 * Night Light, Contrast and the looks, made by the display's own gamma table */
bool plat_screen_colors_here(void);

/* What the system on this card has, a second fact beside the profile's: a
 * card can hold a newer ZyperOS folder on an older system (Flash Drive came
 * with 1.0.4's), so a feature shows when the handheld can do it AND the
 * system has its part. "usbdrive" (Flash Drive's program), "hostapd" (the
 * hotspot's); any other part false. Read from the image's "has" line in
 * /etc/zyperos/system, which plastron writes at the image's build; on an
 * image from before that line, from the program's own file. */
bool plat_system_has(const char *part);

#endif
