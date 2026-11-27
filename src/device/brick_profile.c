/* SPDX-License-Identifier: GPL-3.0-only */
/* The TrimUI Brick (TG3040) as a profile (src/device/profile.h). Its glass:
 * 3.2 in at 1024x768, 65.0 x 48.8 mm. Its own handheld beside the
 * tutorial's lessons and on the Controls tab (src/device/brick_model.c). */
#include "profile.h"
#include "brick_buttons.h"

/* Its core options (src/device/brick_coreopts.c), linked in its build alone */
extern const dev_default dev_coreopts_brick[];

/* The A133's sensors as its device tree names their zones
 * (plastron/buildroot/board/brick/dts/sun50iw10p1-trimui-brick.dts): the
 * CPU's, whose trips slow the CPU at 58 and 63 C and power the Brick off at
 * 90 C, and the GPU's. Its third, the DDR's, is the memory's, not the chip's
 * to warn of. */
static const char *const brick_zones[] = { "cpu_thermal_zone", "gpu_thermal_zone", NULL };

const device_profile device_profile_brick = {
	.id = "brick",
	.name = "TrimUI Brick",
	/* Allwinner's Linux 4.9 for the A133 (plastron/THIRD-PARTY.md) */
	.kernel = { "Allwinner", "Brick" },
	.userdata = "brick",
	.boot_label = "BRICKBOOT",
	.screen = {
		.panel_w = 1024, .panel_h = 768, .turn = 0,
		.active_w_mm = 65.0f, .active_h_mm = 48.8f,
		.aspect_w = 4, .aspect_h = 3,
		.still = false,
	},
	.chords = { .bright = DEV_BRIGHT_KEYS },
	.buttons = brick_buttons,
	.n_buttons = (int)(sizeof brick_buttons / sizeof brick_buttons[0]),
	.model = &td_model_brick,
	.bluetooth = true,
	/* The old screens' shaders and the GPU consoles on the GPU path (#312),
	 * tried on real Bricks in the test kits (the picture's way up fixed in
	 * #422) and on in releases from 1.1.0 (2026-11-21, Tarik: "i think a
	 * tester checked it on the brick. it's all good. cant we enabled it?") */
	.gpu_shaders = true,
	.gpu_frames = true,
	/* The Pixel 2's, but its rumble, until a Brick's own are measured
	 * (src/device/brick_coreopts.c) */
	.coreopts = dev_coreopts_brick,
	.speaker_mono = false,
	.mute_switch = true,   /* the side switch, gpio243, 1 when down */
	.rumble = false,   /* GPIO 227 on bldo3, not run until the tester's buzz (Brick parity item 14) */
	.player2 = false,
	.usb_shared_port = false,
	/* The A133's TCON has a gamma table, set through /dev/disp
	 * (src/panelgamma_sunxi.h), and its display engine a saturation */
	.screen_colors = true,
	.wifi = DEV_WIFI_BUILTIN,
	.thermal_zones = brick_zones,
	/* 3.40 V, a careful guess, nothing measured: no cutoff is set or named
	 * for its battery (its kernel's axp2202_charger.c sets no power-off
	 * voltage and its device tree gives none; Allwinner's other AXP drivers
	 * default to 3.3 V), a 4.2 V cell whose own gauge reads its level */
	.batt_empty_mv = 3400,
};
