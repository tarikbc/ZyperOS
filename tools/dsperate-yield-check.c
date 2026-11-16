/* The idle-loop yield in a built DSperate core (mk/dsperate-yield-check.sh,
 * which mk/build-dsperate.sh runs on each core it builds).
 *
 * mk/patches/dsperate-lr-idle-yield.patch makes a CPU that only polls give
 * up the rest of its slice (DSperate's cpu/idle_loop.h): a loop the
 * recompiler translates as one block that branches back to its own start,
 * with no store, no load that pops and no register carried round, spends the
 * slice's budget at once from its second iteration. Giving up a slice only
 * waits, so a poll must still see every change the other CPU, an event or
 * the hardware makes. This boots a ROM it writes itself, with the
 * recompiler on, in which:
 *
 *   - the ARM7 counts down a delay (a loop that carries a register, so never
 *     a poll), then sets a word in main RAM; the ARM9 polls that word in an
 *     ARM loop, and on seeing it writes a marker;
 *   - then each counts VBlanks by polling DISPSTAT, the ARM9 in ARM code and
 *     the ARM7 in Thumb code: wait for the VBlank flag, wait for it to
 *     clear, count one, and again.
 *
 * Run once with dsperate_idle_yield disabled and once enabled, each the
 * same number of frames: the marker is there and both counts are one a
 * frame, in both; and the frames cost no more with the yield than without.
 * Exit 0 when every check holds. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "libretro.h"

static char sysdir[512], savedir[512];
static int fails;
static const char *yield_opt = "enabled";

static void core_log(enum retro_log_level l, const char *fmt, ...)
{
	va_list a;

	if (l < RETRO_LOG_INFO) return;
	va_start(a, fmt);
	printf("    core: ");
	vprintf(fmt, a);
	va_end(a);
}

static bool env(unsigned cmd, void *data)
{
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char **)data = sysdir; return true;
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:   *(const char **)data = savedir; return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:     return true;
	case RETRO_ENVIRONMENT_SET_VARIABLES:        return true;
	case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:      return true;
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		((struct retro_log_callback *)data)->log = core_log;
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE: {
		struct retro_variable *v = data;

		if (!strcmp(v->key, "dsperate_idle_yield")) { v->value = yield_opt; return true; }
		return false;   /* the recompiler on, its default: the yield is its */
	}
	default: return false;
	}
}

static void video(const void *d, unsigned w, unsigned h, size_t p) { (void)d; (void)w; (void)h; (void)p; }
static void audio1(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio(const int16_t *d, size_t f) { (void)d; return f; }
static void poll_(void) {}
static int16_t input(unsigned a, unsigned b, unsigned c, unsigned d) { (void)a; (void)b; (void)c; (void)d; return 0; }

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

static uint32_t get32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Main RAM words the program uses, from 0x02100000 */
#define FLAG   0x100000u   /* set by the ARM7 after its delay */
#define MARK   0x100004u   /* written by the ARM9 when it saw FLAG */
#define COUNT9 0x100008u   /* VBlanks the ARM9 counted */
#define COUNT7 0x10000Cu   /* and the ARM7 */
#define GOOD   0x600D600Du

/* The ARM9's code at 0x02000000 (ROM 0x8000) and the ARM7's at 0x037F8000
 * (ROM 0x9000), hand-assembled; see the comment at the top for what each
 * does. Each poll is its own block: a load, a test, a branch back. */
static const uint32_t ARM9[] = {
	0xE59F1048,   /* 00 ldr r1, =0x02100000   FLAG          */
	0xE59F2048,   /* 04 ldr r2, =0x02100004   MARK          */
	0xE59F3048,   /* 08 ldr r3, =GOOD                       */
	0xE59F4048,   /* 0C ldr r4, =0x04000004   DISPSTAT      */
	0xE59F6048,   /* 10 ldr r6, =0x02100008   COUNT9        */
	0xE3A05000,   /* 14 mov r5, #0                          */
	0xE5910000,   /* 18 poll: ldr r0, [r1]                  */
	0xE3500000,   /* 1C cmp r0, #0                          */
	0x0AFFFFFC,   /* 20 beq poll                            */
	0xE5823000,   /* 24 str r3, [r2]                        */
	0xE1D400B0,   /* 28 vset: ldrh r0, [r4]                 */
	0xE3100001,   /* 2C tst r0, #1                          */
	0x0AFFFFFC,   /* 30 beq vset                            */
	0xE1D400B0,   /* 34 vclr: ldrh r0, [r4]                 */
	0xE3100001,   /* 38 tst r0, #1                          */
	0x1AFFFFFC,   /* 3C bne vclr                            */
	0xE2855001,   /* 40 add r5, r5, #1                      */
	0xE5865000,   /* 44 str r5, [r6]                        */
	0xEAFFFFF6,   /* 48 b vset                              */
	0xE1A00000,   /* 4C nop                                 */
	0x02100000, 0x02100004, GOOD, 0x04000004, 0x02100008,   /* 50.. */
};

static const uint32_t ARM7[] = {
	0xE59F1020,   /* 00 ldr r1, =0x02100000   FLAG          */
	0xE59F2020,   /* 04 ldr r2, =DELAY                      */
	0xE2522001,   /* 08 delay: subs r2, r2, #1              */
	0x1AFFFFFD,   /* 0C bne delay                           */
	0xE3A00001,   /* 10 mov r0, #1                          */
	0xE5810000,   /* 14 str r0, [r1]                        */
	0xE59F4010,   /* 18 ldr r4, =0x04000004   DISPSTAT      */
	0xE59F6010,   /* 1C ldr r6, =0x0210000C   COUNT7        */
	0xE28F0019,   /* 20 add r0, pc, #0x19     (0x40, Thumb) */
	0xE12FFF10,   /* 24 bx r0                               */
	0x02100000, 3000000, 0x04000004, 0x0210000C,   /* 28..34 */
	0xE1A00000, 0xE1A00000,                        /* 38, 3C */
};

static const uint16_t ARM7_THUMB[] = {   /* at 0x40 */
	0x2101,   /* 40 mov r1, #1                              */
	0x2500,   /* 42 mov r5, #0                              */
	0x8820,   /* 44 vset: ldrh r0, [r4]                     */
	0x4208,   /* 46 tst r0, r1                              */
	0xD0FC,   /* 48 beq vset                                */
	0x8820,   /* 4A vclr: ldrh r0, [r4]                     */
	0x4208,   /* 4C tst r0, r1                              */
	0xD1FC,   /* 4E bne vclr                                */
	0x3501,   /* 50 add r5, #1                              */
	0x6035,   /* 52 str r5, [r6]                            */
	0xE7F6,   /* 54 b vset                                  */
	0x46C0,   /* 56 nop                                     */
};

static int write_rom(const char *path)
{
	static uint8_t rom[0xA000];
	FILE *f;
	size_t i;

	memcpy(rom, "ZYPEROS YLD ZYLD01", 18);
	put32(rom + 0x20, 0x8000);      put32(rom + 0x24, 0x02000000);
	put32(rom + 0x28, 0x02000000);  put32(rom + 0x2C, sizeof ARM9);
	put32(rom + 0x30, 0x9000);      put32(rom + 0x34, 0x037F8000);
	put32(rom + 0x38, 0x037F8000);  put32(rom + 0x3C, 0x40 + sizeof ARM7_THUMB);
	put32(rom + 0x80, sizeof rom);  put32(rom + 0x84, 0x4000);
	for (i = 0; i < sizeof ARM9 / 4; i++) put32(rom + 0x8000 + 4 * i, ARM9[i]);
	for (i = 0; i < sizeof ARM7 / 4; i++) put32(rom + 0x9000 + 4 * i, ARM7[i]);
	for (i = 0; i < sizeof ARM7_THUMB / 2; i++) put16(rom + 0x9040 + 2 * i, ARM7_THUMB[i]);
	f = fopen(path, "wb");
	if (!f || fwrite(rom, 1, sizeof rom, f) != sizeof rom) return -1;
	return fclose(f);
}

static void check(int ok, const char *what)
{
	printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok) fails++;
}

static double now_s(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

struct run { uint32_t mark, count9, count7; double secs; };

int main(int argc, char **argv)
{
	void *so;
	void (*set_env)(retro_environment_t);
	void (*set_video)(retro_video_refresh_t), (*set_audio1)(retro_audio_sample_t);
	void (*set_audio)(retro_audio_sample_batch_t), (*set_poll)(retro_input_poll_t);
	void (*set_input)(retro_input_state_t), (*init)(void), (*run)(void);
	bool (*load)(const struct retro_game_info *);
	void (*unload)(void);
	void *(*mem)(unsigned);
	char rom[600];
	struct run r[2];
	const int FRAMES = 240;
	int k, f;

	if (argc < 3) { fprintf(stderr, "usage: %s core.so dir\n", argv[0]); return 2; }
	snprintf(sysdir, sizeof sysdir, "%s/sys", argv[2]);
	snprintf(savedir, sizeof savedir, "%s/save", argv[2]);
	snprintf(rom, sizeof rom, "%s/yield.nds", argv[2]);
	{
		char cmd[1200];

		snprintf(cmd, sizeof cmd, "mkdir -p '%s' '%s'", sysdir, savedir);
		if (system(cmd)) return 2;
	}
	if (write_rom(rom)) { perror(rom); return 2; }
	if (!(so = dlopen(argv[1], RTLD_NOW))) { fprintf(stderr, "%s\n", dlerror()); return 2; }
	set_env = dlsym(so, "retro_set_environment");
	set_video = dlsym(so, "retro_set_video_refresh");
	set_audio1 = dlsym(so, "retro_set_audio_sample");
	set_audio = dlsym(so, "retro_set_audio_sample_batch");
	set_poll = dlsym(so, "retro_set_input_poll");
	set_input = dlsym(so, "retro_set_input_state");
	init = dlsym(so, "retro_init");
	run = dlsym(so, "retro_run");
	load = dlsym(so, "retro_load_game");
	unload = dlsym(so, "retro_unload_game");
	mem = dlsym(so, "retro_get_memory_data");
	if (!set_env || !run || !load || !unload || !mem) { fprintf(stderr, "not a libretro core\n"); return 2; }
	set_env(env); set_video(video); set_audio1(audio1); set_audio(audio); set_poll(poll_); set_input(input);
	init();

	printf("the idle-loop yield, %d frames each, the recompiler on\n", FRAMES);
	for (k = 0; k < 2; k++) {
		struct retro_game_info g = { .path = rom };
		const uint8_t *ram;
		double t0;

		yield_opt = k ? "enabled" : "disabled";
		if (!load(&g)) { fprintf(stderr, "the core would not load the ROM\n"); return 2; }
		t0 = now_s();
		for (f = 0; f < FRAMES; f++) run();
		r[k].secs = now_s() - t0;
		ram = mem(RETRO_MEMORY_SYSTEM_RAM);
		r[k].mark = ram ? get32(ram + MARK) : 0;
		r[k].count9 = ram ? get32(ram + COUNT9) : 0;
		r[k].count7 = ram ? get32(ram + COUNT7) : 0;
		printf("  yield %-8s marker %08X, VBlanks counted ARM9 %u, ARM7 %u, %.3f s (%.0f frames/s)\n",
		       yield_opt, r[k].mark, r[k].count9, r[k].count7, r[k].secs, FRAMES / r[k].secs);
		unload();
	}
	for (k = 0; k < 2; k++) {
		char what[160];

		snprintf(what, sizeof what, "yield %s: the ARM9's poll saw the ARM7's word", k ? "on" : "off");
		check(r[k].mark == GOOD, what);
		/* The delay is a few frames; the rest count one VBlank each */
		snprintf(what, sizeof what, "yield %s: both CPUs' DISPSTAT polls saw each VBlank (ARM and Thumb)", k ? "on" : "off");
		check(r[k].count9 >= (uint32_t)FRAMES - 60 && r[k].count9 <= (uint32_t)FRAMES &&
		      r[k].count7 >= (uint32_t)FRAMES - 60 && r[k].count7 <= (uint32_t)FRAMES, what);
	}
	check(r[1].count9 + 1 >= r[0].count9 && r[1].count9 <= r[0].count9 + 1 &&
	      r[1].count7 + 1 >= r[0].count7 && r[1].count7 <= r[0].count7 + 1,
	      "the same counts, a frame either way, with the yield as without");
	check(r[1].secs <= r[0].secs * 1.1, "and the frames cost no more with the yield");
	printf("  the yield's frames took %.0f%% of the time without it\n", 100.0 * r[1].secs / r[0].secs);
	printf(fails ? "%d FAILED\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
