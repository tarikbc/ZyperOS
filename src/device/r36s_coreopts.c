/* SPDX-License-Identifier: GPL-3.0-only */
/* The R36S's core options (src/device/profile.h, coreopts), given at run
 * time under the player's own rows (src/coreopts.h); the ones every device
 * takes are in src/coreopts.c.
 *
 * None is measured on an R36S yet. It has the Pixel 2's chip and GPU, the
 * RK3326 and its Mali-G31 under Panfrost, so it starts with the Pixel 2's
 * (src/device/pixel2_coreopts.c), whose measurements are written there.
 * Left out: the Pixel 2's rumble (the N64's Rumble Pak in the controller,
 * PrBoom's rumble): the R36S has no motor, so the cores keep their own
 * defaults. */
#include "profile.h"

const dev_default dev_coreopts_r36s[] = {
	/* flycast2021, the Dreamcast's, at 320x240 */
	{ "coreopt..reicast_internal_resolution", "320x240" },
	/* Mupen64Plus-Next, the Nintendo 64's second core: GLideN64 at 320x240,
	 * and no framebuffer emulation (it crashed Mesa's panfrost on the
	 * Pixel 2's same GPU) */
	{ "coreopt..mupen64plus-43screensize", "320x240" },
	{ "coreopt.N64.mupen64plus-EnableFBEmulation", "False" },
	/* ParallelN64, the Nintendo 64's default */
	{ "coreopt.N64.parallel-n64-gfxplugin", "glide64" },
	{ "coreopt.N64.parallel-n64-gfxplugin-accuracy", "medium" },
	{ "coreopt.N64.parallel-n64-framerate", "fullspeed" },
	{ "coreopt.N64.parallel-n64-rspplugin", "hle" },
	{ "coreopt.N64.parallel-n64-cpucore", "dynamic_recompiler" },
	{ "coreopt.N64.parallel-n64-screensize", "320x240" },
	/* The PlayStation's DualShock turns analog on L3+R3, the two sticks'
	 * clicks together, which a game's own buttons do not press as they
	 * can L1+R1+SELECT, the core's default */
	{ "coreopt.PS1.pcsx_rearmed_analog_combo", "l3+r3" },
	/* Opera, the 3DO's: its DSP on a thread of its own */
	{ "coreopt..opera_dsp_threaded", "enabled" },
	/* PPSSPP, the PSP's, as on the Pixel 2 */
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
