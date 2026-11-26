/* SPDX-License-Identifier: GPL-3.0-only */
/* The R36S as a profile (src/device/profile.h): a start, for volunteers to
 * take further (docs/devices/r36s.md says what is tried and what is not).
 * The Pixel 2's chip, the RK3326, with its Mali-G31, and the same RK817 for
 * power and sound; a 3.5 in 640x480 panel that scans as it is seen, not
 * turned (ROCKNIX's R36S panel mode is 640x480, and its device tree turns
 * nothing); two sticks; and no radio of its own (rocknix.org's R35S/R36S
 * page: "Connectivity: None").
 *
 * FN is the launcher's MENU, a round key with the word printed over it:
 * the Pixel 2's FUNCTION, seen when it is let go, and brightness is FN with
 * a volume key, as the R36S has no keys of its own for it. It has one light
 * and no side lights, so L2 and R2 are plain keys. Its left stick moves as
 * the d-pad does on the launcher's screens (src/device/pixel2.c, its
 * stickpad), its right stick does nothing there, and its clicks are L3 and
 * R3, which the launcher names and draws; in a game Diatom gives the core
 * both sticks and both clicks. */
#include "profile.h"

/* Its core options (src/device/r36s_coreopts.c), linked in its build alone */
extern const dev_default dev_coreopts_r36s[];

static const dev_button r36s_buttons[] = {
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
	/* The sticks, pressed: drawn as the stick, its name on it */
	{ IN_L3,     "L3",     DEV_CAP_STICK,    DEV_ICON_NONE, 0 },
	{ IN_R3,     "R3",     DEV_CAP_STICK,    DEV_ICON_NONE, 0 },
	/* Round blank keys, the words printed on the body under them: the
	 * glyph is the word */
	{ IN_SELECT, "SELECT", DEV_CAP_ROUND,    DEV_ICON_NONE, 0 },
	{ IN_START,  "START",  DEV_CAP_ROUND,    DEV_ICON_NONE, 0 },
	/* FN, printed over a round blank key */
	{ IN_MENU,   "FN",     DEV_CAP_ROUND,    DEV_ICON_NONE, 0 },
	{ IN_VOLUP,  "VOL+",   DEV_CAP_ROCKER,   DEV_ICON_NONE, 0 },
	{ IN_VOLDN,  "VOL-",   DEV_CAP_ROCKER,   DEV_ICON_NONE, 0 },
	{ IN_POWER,  "POWER",  DEV_CAP_PILL,     DEV_ICON_NONE, 0 },
};

/* The RK3326's two sensors as px30.dtsi names their zones, as on the
 * Pixel 2 */
static const char *const r36s_zones[] = { "soc-thermal", "gpu-thermal", NULL };

const device_profile device_profile_r36s = {
	.id = "r36s",
	.name = "R36S",
	/* ROCKNIX's RK3326 patches, its R36S device tree (from JELOS) */
	.kernel = { "ROCKNIX", "R36S" },
	.userdata = "r36s",
	.boot_label = "R36SBOOT",
	.screen = {
		/* 640x480 as it scans. Its glass: 3.5 in at 4:3. ROCKNIX's mode
		 * says 52 x 70 mm, the two the other way round. */
		.panel_w = 640, .panel_h = 480, .turn = 0,
		.active_w_mm = 70.0f, .active_h_mm = 52.5f,
		.aspect_w = 4, .aspect_h = 3,
		.still = true,
	},
	.chords = {
		.bright = DEV_BRIGHT_MENU_VOL,
		.menu_on_release = true,
		.lr2_taps = false,
		.home_chord = true,
		.battery_chord = false,
		.stick_dpad = true,
	},
	.buttons = r36s_buttons,
	.n_buttons = (int)(sizeof r36s_buttons / sizeof r36s_buttons[0]),
	.model = &td_model_r36s,
	.bluetooth = false,
	/* The Pixel 2's GPU and driver (Panfrost on the Mali-G31): the old
	 * screens' pass and the GPU consoles as there, not yet tried on an
	 * R36S */
	.gpu_shaders = true,
	.gpu_frames = true,
	.coreopts = dev_coreopts_r36s,
	.speaker_mono = true,   /* one speaker, between the d-pad and A B X Y */
	/* None: ROCKNIX's R36S joypad sets no rumble and its tree has no
	 * pwm-vibrator */
	.rumble = false,
	.player2 = false,       /* not built or tried on it */
	/* Its OTG port is for the computer and accessories alike; its DC port
	 * charges */
	.usb_shared_port = true,
	/* The PX30 VOP's gamma table, as the Pixel 2's (the same display
	 * block), not yet tried on an R36S */
	.screen_colors = true,
	/* No radio of its own; a USB adapter on the OTG port, as the Pixel 2
	 * takes one, not yet tried on an R36S */
	.wifi = DEV_WIFI_ADAPTER,
	.thermal_zones = r36s_zones,
	/* None until a real R36S's cutoff is measured: ROCKNIX's battery table
	 * for it (rk3326-gameconsole-r3xs.dtsi, the BSP's curve) puts 3.38 V at
	 * 20%, under the design minimum its trees give (3.50 V in its 20260901
	 * release, 3.40 V since), so the Pixel 2's 3.45 V would power it off
	 * with a fifth of the battery left. The level's rule alone. */
	.batt_empty_mv = 0,
};
