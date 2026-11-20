/* SPDX-License-Identifier: GPL-3.0-only */
/* The TrimUI Smart Pro (TG5040) as a profile (src/device/profile.h). It is
 * the Brick's board (the same A133 family and plastron's kernel for it) with
 * its own panel: 4.96 in at 720x1280, mounted turned, 109.8 x 61.8 mm of
 * glass (a 16:9 panel of that diagonal). Its canvas is 16:9 too, 1368 x 768
 * (src/canvas.h), scaled onto the whole panel (src/device/brick.c).
 *
 * From a static read of TrimUI's firmware 1.1.1 (the plan's Task 1): the
 * panel is turned a quarter, 270 as on the Pixel 2 (TrimUI's kernel turns
 * its 1280x720 framebuffer by 3, 270 degrees, onto the panel), and its
 * thermal zones are the Brick's (the same device tree nodes). Which way a
 * quarter turn goes is still to see on a Smart Pro; only that it is a
 * quarter is used yet (src/device/brick.c). Its own handheld beside the
 * tutorial's lessons and on the Controls tab (src/device/smartpro_model.c).
 *
 * Its controls: every button and both sticks come through two small
 * controllers on serial ports (the plan's Task 3), which ZyperOS's reader
 * turns into one gamepad. It has no brightness keys, so brightness is MENU
 * with a volume key, as on the Pixel 2, and MENU is seen when it is let go.
 * The sticks have no launcher button of their own (src/inbutton.h): the
 * left one is the d-pad's (src/stickpad.h). As printed (TrimUI's manual):
 * SELECT a square, START a play triangle, MENU TrimUI's mark, so its word
 * (no vendor's logo is a glyph), as on the Brick. */
#include "profile.h"

/* The Brick's core options (src/device/brick_coreopts.c), which its build
 * links too: the same A133P and PowerVR GE8300, and none measured on either
 * yet */
extern const dev_default dev_coreopts_brick[];

static const dev_button smartpro_buttons[] = {
	{ IN_UP,     "UP",     DEV_CAP_DPAD,     DEV_ICON_NONE, 0 },
	{ IN_DOWN,   "DOWN",   DEV_CAP_DPAD,     DEV_ICON_NONE, 0 },
	{ IN_LEFT,   "LEFT",   DEV_CAP_DPAD,     DEV_ICON_NONE, 0 },
	{ IN_RIGHT,  "RIGHT",  DEV_CAP_DPAD,     DEV_ICON_NONE, 0 },
	{ IN_ACCEPT, "A",      DEV_CAP_ROUND,    DEV_ICON_NONE, 0 },
	{ IN_BACK,   "B",      DEV_CAP_ROUND,    DEV_ICON_NONE, 0 },
	{ IN_X,      "X",      DEV_CAP_ROUND,    DEV_ICON_NONE, 0 },
	{ IN_Y,      "Y",      DEV_CAP_ROUND,    DEV_ICON_NONE, 0 },
	{ IN_L1,     "L1",     DEV_CAP_SHOULDER, DEV_ICON_NONE, 0 },
	{ IN_R1,     "R1",     DEV_CAP_SHOULDER, DEV_ICON_NONE, 0 },
	{ IN_L2,     "L2",     DEV_CAP_SHOULDER, DEV_ICON_NONE, 0 },
	{ IN_R2,     "R2",     DEV_CAP_SHOULDER, DEV_ICON_NONE, 0 },
	{ IN_SELECT, "SELECT", DEV_CAP_ROUND,    DEV_ICON_SQUARE, 0 },
	{ IN_START,  "START",  DEV_CAP_ROUND,    DEV_ICON_PLAY,   0 },
	{ IN_MENU,   "MENU",   DEV_CAP_ROUND,    DEV_ICON_NONE, 0 },
	{ IN_VOLUP,  "VOL+",   DEV_CAP_PILL,     DEV_ICON_NONE, 0 },
	{ IN_VOLDN,  "VOL-",   DEV_CAP_PILL,     DEV_ICON_NONE, 0 },
	{ IN_POWER,  "POWER",  DEV_CAP_ROUND,    DEV_ICON_NONE, 0 },
};

/* The A133's zones as the Brick's device tree names them, until the Smart
 * Pro's own is read (the plan's Task 1) */
static const char *const smartpro_zones[] = { "cpu_thermal_zone", "gpu_thermal_zone", NULL };

const device_profile device_profile_smartpro = {
	.id = "smartpro",
	.name = "TrimUI Smart Pro",
	/* Allwinner's Linux 4.9 for the A133, the Brick's board's
	 * (plastron/THIRD-PARTY.md) */
	.kernel = { "Allwinner", "Smart Pro" },
	.userdata = "smartpro",
	.boot_label = "SMARTBOOT",
	.screen = {
		.panel_w = 720, .panel_h = 1280, .turn = 270,
		.active_w_mm = 109.8f, .active_h_mm = 61.8f,
		.aspect_w = 16, .aspect_h = 9,
		.still = false,
	},
	.chords = {
		.bright = DEV_BRIGHT_MENU_VOL,
		.menu_on_release = true,
	},
	.buttons = smartpro_buttons,
	.n_buttons = (int)(sizeof smartpro_buttons / sizeof smartpro_buttons[0]),
	.model = &td_model_smartpro,
	.bluetooth = true,
#ifdef ZYPEROS_KIT_BUILD
	/* A test kit (src/kit.h): the old screens' shaders and the GPU consoles
	 * on the GPU path, as the Brick's kit has them */
	.gpu_shaders = true,
	.gpu_frames = true,
#else
	.gpu_shaders = false,
	/* Off as on the Brick, whose PowerVR it shares */
	.gpu_frames = false,
#endif
	.coreopts = dev_coreopts_brick,
	.speaker_mono = false,
	/* GPIO 227 on bldo3, as the Brick's: not run until a tester has felt it */
	.rumble = false,
	.player2 = false,
	.usb_shared_port = false,
	/* The A133's TCON gamma table and its display engine's saturation, as
	 * on the Brick (src/panelgamma_sunxi.h) */
	.screen_colors = true,
	.wifi = DEV_WIFI_BUILTIN,
	.thermal_zones = smartpro_zones,
};
