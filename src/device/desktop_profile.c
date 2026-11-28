/* SPDX-License-Identifier: GPL-3.0-only */
/* The host build's own profile (src/device/profile.h): what the launcher run
 * on a computer has always answered, so every shot taken before profiles is
 * taken the same after them. Mostly the Brick, whose device file the host
 * build runs (its host half, src/device/brick.c): its folder, its chords and
 * buttons, Bluetooth, two speakers, Wi-Fi of its own. And two of the Pixel
 * 2's, which the host borrowed to show the Pixel 2's screens: the GPU's old
 * screens in Display's Shader row (they were offered when headless, which
 * every shot is; a windowed run offers them now too), and the 3D handheld
 * on the in-game Controls tab. The handheld is borrowed (model_borrowed):
 * the tutorial shows it only with --fn-volume, as before (src/profile.c,
 * dev_tutorial_model). Shots of a real handheld wear its own profile:
 * --device pixel2, --device brick. */
#include "profile.h"
#include "brick_input.h"

/* The Brick's controls (brick_buttons.h: F1 and F2 for brightness), drawn as
 * the borrowed handheld prints them, the Pixel 2's: SELECT and START round
 * keys printed SE and ST, MENU round with three dots (src/ds/button.h) */
static const dev_button desktop_buttons[] = {
	{ IN_UP,       "UP",     DEV_CAP_DPAD,     DEV_ICON_NONE, 0 },
	{ IN_DOWN,     "DOWN",   DEV_CAP_DPAD,     DEV_ICON_NONE, 0 },
	{ IN_LEFT,     "LEFT",   DEV_CAP_DPAD,     DEV_ICON_NONE, 0 },
	{ IN_RIGHT,    "RIGHT",  DEV_CAP_DPAD,     DEV_ICON_NONE, 0 },
	{ IN_ACCEPT,   "A",      DEV_CAP_ROUND,    DEV_ICON_NONE, 0 },
	{ IN_BACK,     "B",      DEV_CAP_ROUND,    DEV_ICON_NONE, 0 },
	{ IN_X,        "X",      DEV_CAP_ROUND,    DEV_ICON_NONE, 0 },
	{ IN_Y,        "Y",      DEV_CAP_ROUND,    DEV_ICON_NONE, 0 },
	{ IN_L1,       "L1",     DEV_CAP_SHOULDER, DEV_ICON_NONE, 0 },
	{ IN_R1,       "R1",     DEV_CAP_SHOULDER, DEV_ICON_NONE, 0 },
	{ IN_L2,       "L2",     DEV_CAP_SHOULDER, DEV_ICON_NONE, 0 },
	{ IN_R2,       "R2",     DEV_CAP_SHOULDER, DEV_ICON_NONE, 0 },
	{ IN_SELECT,   "SE",     DEV_CAP_ROUND,    DEV_ICON_NONE, 0 },
	{ IN_START,    "ST",     DEV_CAP_ROUND,    DEV_ICON_NONE, 0 },
	{ IN_MENU,     "MENU",   DEV_CAP_ROUND,    DEV_ICON_DOTS, 0 },
	{ IN_BRIGHTDN, "F1",     DEV_CAP_PILL,     DEV_ICON_NONE, 0 },
	{ IN_BRIGHTUP, "F2",     DEV_CAP_PILL,     DEV_ICON_NONE, 0 },
	{ IN_VOLUP,    "VOL+",   DEV_CAP_PILL,     DEV_ICON_NONE, 0 },
	{ IN_VOLDN,    "VOL-",   DEV_CAP_PILL,     DEV_ICON_NONE, 0 },
	{ IN_POWER,    "POWER",  DEV_CAP_ROUND,    DEV_ICON_NONE, 0 },
};

const device_profile device_profile_desktop = {
	.id = "desktop",
	.name = "Desktop",
	/* The Pixel 2's, as the host's About shots always showed */
	.kernel = { "ROCKNIX", "Pixel 2" },
	.userdata = "brick",
	.boot_label = "BRICKBOOT",
	.screen = {
		.panel_w = 1024, .panel_h = 768, .turn = 0,
		.active_w_mm = 65.0f, .active_h_mm = 48.8f,
		.aspect_w = 4, .aspect_h = 3,
		.still = false,
	},
	.chords = { .bright = DEV_BRIGHT_KEYS },
	.input = BRICK_INPUT,
	.buttons = desktop_buttons,
	.n_buttons = (int)(sizeof desktop_buttons / sizeof desktop_buttons[0]),
	.model = &td_model_pixel2,
	.model_borrowed = true,
	.bluetooth = true,
	.gpu_shaders = true,
	.gpu_frames = true,
	.coreopts = dev_coreopts_pixel2,
	.speaker_mono = false,
	.rumble = true,   /* none, but the host shows the rows the Pixel 2 has */
	.player2 = false,
	.usb_shared_port = false,
	.screen_colors = false,
	.wifi = DEV_WIFI_BUILTIN,
	/* None: the computer's chip is not the launcher's to warn of, and the
	 * host's plat_chip_temp reads only the test file */
	.thermal_zones = NULL,
	/* None: the computer has no battery of the launcher's (a test file's
	 * level alone, plat_battery) */
	.batt_empty_mv = 0,
};
