/* SPDX-License-Identifier: GPL-3.0-only */
/* The TrimUI Brick's core options (src/device/profile.h, coreopts), given
 * at run time under the player's own rows (src/coreopts.h); the ones every
 * device takes are in src/coreopts.c. The Smart Pro takes these too: the
 * same chip and GPU, and its build links the Brick's platform layer.
 *
 * None is measured on a Brick yet. They start as the Pixel 2's
 * (src/device/pixel2_coreopts.c), whose reasons are written there: those
 * were tuned for frames on an RK3326 with a Mali-G31, and the Brick's A133
 * (four Cortex-A53 at up to 1.8 GHz) and its PowerVR GE8300 are as fast or
 * faster, so each keeps full speed at least where the Pixel 2 had it. A Brick
 * test kit's logs are what moves one (a resolution up where frames are to
 * spare), here, with the measurement beside it.
 *
 * Left out: the Pixel 2's rumble (the N64's Rumble Pak in the controller,
 * PrBoom's rumble). The Brick has a motor (Tarik, 2026-11-18), but its port
 * drives none until a tester has felt it (the Brick parity plan's item 14),
 * so the cores keep their own defaults: the N64's Controller Pak, Doom's
 * rumble off. */
#include "profile.h"

const dev_default dev_coreopts_brick[] = {
	/* flycast2021, the Dreamcast's: 320x240, as on the Pixel 2, until a
	 * Brick's frames say 640x480 has room */
	{ "coreopt..reicast_internal_resolution", "320x240" },
	/* Mupen64Plus-Next, the Nintendo 64's second core: GLideN64 at the
	 * N64's own 320x240, and no framebuffer emulation (on the Pixel 2 it
	 * crashed Mesa's panfrost; on any GPU it is the slower way) */
	{ "coreopt..mupen64plus-43screensize", "320x240" },
	{ "coreopt.N64.mupen64plus-EnableFBEmulation", "False" },
	/* ParallelN64, the Nintendo 64's default: Glide64 at medium accuracy,
	 * a frame only for a new picture, the HLE RSP, the dynarec, 320x240 */
	{ "coreopt.N64.parallel-n64-gfxplugin", "glide64" },
	{ "coreopt.N64.parallel-n64-gfxplugin-accuracy", "medium" },
	{ "coreopt.N64.parallel-n64-framerate", "fullspeed" },
	{ "coreopt.N64.parallel-n64-rspplugin", "hle" },
	{ "coreopt.N64.parallel-n64-cpucore", "dynamic_recompiler" },
	{ "coreopt.N64.parallel-n64-screensize", "320x240" },
	/* The PlayStation's DualShock turns analog on L3+R3, which the Brick has
	 * not either (no sticks), so a game's own buttons never press it */
	{ "coreopt.PS1.pcsx_rearmed_analog_combo", "l3+r3" },
	/* Opera, the 3DO's: its DSP on a thread of its own, one of the A133's
	 * four cores */
	{ "coreopt..opera_dsp_threaded", "enabled" },
	/* PPSSPP, the PSP's: OpenGL ES, the JIT and fast memory, the PSP's own
	 * 480x272, every frame drawn, hardware transform, software skinning,
	 * lazy texture caching, low spline quality, GPU readbacks skipped, no
	 * anisotropic filtering, no texture scaling */
	{ "coreopt.PSP.ppsspp_backend",                       "opengl" },
	{ "coreopt.PSP.ppsspp_cpu_core",                      "JIT" },
	{ "coreopt.PSP.ppsspp_fast_memory",                   "enabled" },
	{ "coreopt.PSP.ppsspp_internal_resolution",           "480x272" },
	{ "coreopt.PSP.ppsspp_auto_frameskip",                "disabled" },
	{ "coreopt.PSP.ppsspp_frameskip",                     "disabled" },
	{ "coreopt.PSP.ppsspp_frame_duplication",             "disabled" },
	{ "coreopt.PSP.ppsspp_gpu_hardware_transform",        "enabled" },
	{ "coreopt.PSP.ppsspp_software_skinning",             "enabled" },
	{ "coreopt.PSP.ppsspp_lazy_texture_caching",          "enabled" },
	{ "coreopt.PSP.ppsspp_spline_quality",                "Low" },
	{ "coreopt.PSP.ppsspp_skip_gpu_readbacks",            "enabled" },
	{ "coreopt.PSP.ppsspp_texture_anisotropic_filtering", "disabled" },
	{ "coreopt.PSP.ppsspp_texture_scaling_level",         "disabled" },
	{ NULL, NULL },
};
