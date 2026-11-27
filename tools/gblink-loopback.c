/* SPDX-License-Identifier: GPL-3.0-only */
/* Two Gambatte cores linked over loopback, on a computer: the Link Cable's
 * core side (src/link.h, mk/patches/gambatte-link.patch) without two
 * handhelds.
 *
 *     cc -O2 -o gblink-loopback tools/gblink-loopback.c -Idiatom/src -ldl
 *     ./gblink-loopback path/to/gambatte_libretro.{so,dylib}
 *
 * The core is the pinned gambatte-libretro with the patch, built for the
 * computer (`make platform=osx` or `platform=unix` in its tree). Two
 * processes each load it with a ROM made here, a few bytes of Game Boy code:
 * the server's game clocks a byte out every transfer (the master, internal
 * clock) and counts, the client's waits for one (the slave, external clock)
 * and counts, each keeping what it got at $C000 and its count at $C001. The
 * core options are the ones the launcher sends (link_opts): the server's
 * "Network Server", the client's "Network Client" and the digits of
 * 127.0.0.1.
 *
 *   A  both run: the client's count goes up (bytes cross the link) and the
 *      server's last byte is one the client sent
 *   B  the client is stopped (SIGSTOP), as a handheld gone out of range:
 *      the server's game must not freeze for longer than the patch's ten
 *      seconds, and runs on after
 *   C  the client goes on (SIGCONT): the link comes back by itself
 *   D  the client unloads its game: the server reads 0xFF (a cable out) at
 *      once, with no frame longer than a second
 *
 * Exit 0 when every phase holds. make check-gblink runs it once
 * tools/gblink-core.sh has built the core for the computer (the release
 * does not), and skips without it. */
#include <dlfcn.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "libretro.h"

static const char *g_role;          /* "server" or "client" */
static const char *g_opts[16][2];
static int g_nopts;

static long long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void core_log(enum retro_log_level level, const char *fmt, ...)
{
	va_list ap;

	if (level < RETRO_LOG_INFO) return;
	if (!strstr(fmt, "GameLink") && !strstr(fmt, "socket") && !strstr(fmt, "server")) return;
	fprintf(stderr, "  [%s core] ", g_role);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

static bool env(unsigned cmd, void *data)
{
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_VARIABLE: {
		struct retro_variable *v = data;
		int i;

		for (i = 0; i < g_nopts; i++)
			if (!strcmp(v->key, g_opts[i][0])) { v->value = g_opts[i][1]; return true; }
		v->value = NULL;
		return false;
	}
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		((struct retro_log_callback *)data)->log = core_log;
		return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
		return true;
	case RETRO_ENVIRONMENT_GET_CAN_DUPE:
		*(bool *)data = true;
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
		*(bool *)data = false;
		return true;
	default:
		return false;
	}
}

static void video(const void *d, unsigned w, unsigned h, size_t p) { (void)d; (void)w; (void)h; (void)p; }
static void sample(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t batch(const int16_t *d, size_t f) { (void)d; return f; }
static void poll_in(void) { }
static int16_t state(unsigned p, unsigned d, unsigned i, unsigned id) { (void)p; (void)d; (void)i; (void)id; return 0; }

/* The game: a byte out and one in on every transfer, its count at $C001 and
 * what came in at $C000. `master` clocks (SC 0x81) from 0; the other waits
 * (SC 0x80), counting from 0x80. */
static void make_rom(unsigned char *rom, size_t n, bool master)
{
	static const unsigned char code[] = {
		0xF3,                   /* 150 di */
		0x31, 0xFE, 0xFF,       /* 151 ld sp,$FFFE */
		0x06, 0x00,             /* 154 ld b,start */
		0x04,                   /* 156 loop: inc b */
		0x78,                   /* 157 ld a,b */
		0xE0, 0x01,             /* 158 ldh (SB),a */
		0x3E, 0x81,             /* 15A ld a,SC */
		0xE0, 0x02,             /* 15C ldh (SC),a */
		0xF0, 0x02,             /* 15E wait: ldh a,(SC) */
		0xCB, 0x7F,             /* 160 bit 7,a */
		0x20, 0xFA,             /* 162 jr nz,wait */
		0xF0, 0x01,             /* 164 ldh a,(SB) */
		0xEA, 0x00, 0xC0,       /* 166 ld ($C000),a */
		0x78,                   /* 169 ld a,b */
		0xEA, 0x01, 0xC0,       /* 16A ld ($C001),a */
		0x18, 0xE7,             /* 16D jr loop */
	};
	unsigned char x = 0;
	int i;

	memset(rom, 0, n);
	rom[0x100] = 0x00; rom[0x101] = 0xC3; rom[0x102] = 0x50; rom[0x103] = 0x01;
	memcpy(rom + 0x134, "LINKTEST", 8);
	memcpy(rom + 0x150, code, sizeof code);
	rom[0x150 + 5] = master ? 0x00 : 0x80;
	rom[0x150 + 11] = master ? 0x81 : 0x80;
	for (i = 0x134; i <= 0x14C; i++) x = (unsigned char)(x - rom[i] - 1);
	rom[0x14D] = x;
}

/* One side: load, then frames until `end_ms` after the start, a line a
 * second (time, frames, longest frame, count, last byte in), unloading the
 * game at `unload_ms` if not 0 and running on after */
static int side(const char *core, bool server, long long t0, long long unload_ms, long long end_ms)
{
	static unsigned char rom[0x8000];
	void *h = dlopen(core, RTLD_NOW | RTLD_LOCAL);
	void (*set_env)(retro_environment_t), (*set_video)(retro_video_refresh_t);
	void (*set_sample)(retro_audio_sample_t), (*set_batch)(retro_audio_sample_batch_t);
	void (*set_poll)(retro_input_poll_t), (*set_state)(retro_input_state_t);
	void (*init)(void), (*run)(void), (*unload)(void);
	bool (*load)(const struct retro_game_info *);
	void *(*mem)(unsigned);
	struct retro_game_info gi;
	long long next = t0 + 1000, longest = 0, worst_b = 0, worst_d = 0;
	unsigned long frames = 0, frames_after_b = 0;
	bool unloaded = false;
	/* A stretch of the link holds when what came in, less this side's
	 * count, stays the same from one sample to the next: both games count
	 * one a transfer, so a byte lost or garbled moves it */
	int same_a = 0, same_c = 0, last_diff = -1, ff_after_d = 0, samples_d = 0;

	if (!h) { fprintf(stderr, "%s\n", dlerror()); return 2; }
	*(void **)&set_env = dlsym(h, "retro_set_environment");
	*(void **)&set_video = dlsym(h, "retro_set_video_refresh");
	*(void **)&set_sample = dlsym(h, "retro_set_audio_sample");
	*(void **)&set_batch = dlsym(h, "retro_set_audio_sample_batch");
	*(void **)&set_poll = dlsym(h, "retro_set_input_poll");
	*(void **)&set_state = dlsym(h, "retro_set_input_state");
	*(void **)&init = dlsym(h, "retro_init");
	*(void **)&run = dlsym(h, "retro_run");
	*(void **)&unload = dlsym(h, "retro_unload_game");
	*(void **)&load = dlsym(h, "retro_load_game");
	*(void **)&mem = dlsym(h, "retro_get_memory_data");
	set_env(env);
	init();
	set_video(video); set_sample(sample); set_batch(batch); set_poll(poll_in); set_state(state);
	make_rom(rom, sizeof rom, server);
	memset(&gi, 0, sizeof gi);
	gi.path = "linktest.gb";
	gi.data = rom;
	gi.size = sizeof rom;
	if (!load(&gi)) { fprintf(stderr, "%s: load failed\n", g_role); return 2; }
	for (;;) {
		long long a = now_ms(), took;
		const unsigned char *wram;

		if (a - t0 >= end_ms) break;
		if (unload_ms && !unloaded && a - t0 >= unload_ms) {
			unload();
			unloaded = true;
			printf("%s %5lld unloaded\n", g_role, a - t0);
			fflush(stdout);
		}
		if (!unloaded) {
			run();
			frames++;
		}
		took = now_ms() - a;
		if (took > longest) longest = took;
		usleep(4000);
		if (now_ms() >= next) {
			wram = unloaded ? NULL : mem(RETRO_MEMORY_SYSTEM_RAM);
			{
				long long t = now_ms() - t0;
				int diff = wram ? (wram[0] - wram[1]) & 0xFF : -1;

				printf("%s %5lld frames=%lu longest=%lld count=%u in=0x%02X\n", g_role, t,
				       frames, longest, wram ? wram[1] : 0, wram ? wram[0] : 0);
				if (wram && wram[0] != 0xFF && diff == last_diff) {
					if (t >= 1500 && t < 4000) same_a++;
					if (t >= 19000 && t < 26000) same_c++;
				}
				last_diff = wram && wram[0] != 0xFF ? diff : -1;
				if (t >= 4000 && t < 18000 && longest > worst_b) worst_b = longest;
				if (t >= 17500) frames_after_b = frames;
				if (server && t >= 27000) {
					samples_d++;
					if (wram && wram[0] == 0xFF) ff_after_d++;
					if (longest > worst_d) worst_d = longest;
				}
			}
			fflush(stdout);
			next += 1000;
			longest = 0;
		}
	}
	{
		bool ok_a = same_a >= 1, ok_c = same_c >= 3;
		bool ok_b = !server || (worst_b <= 10500 && frames_after_b > 0);
		bool ok_d = !server || (samples_d >= 2 && ff_after_d == samples_d && worst_d <= 1000);

		printf("%s: A %s (%d steady)  B %s (longest frame %lld ms)  C %s (%d steady)  D %s\n",
		       g_role, ok_a ? "ok" : "FAIL", same_a, ok_b ? "ok" : "FAIL", worst_b,
		       ok_c ? "ok" : "FAIL", same_c, ok_d ? "ok" : "FAIL");
		return ok_a && ok_b && ok_c && ok_d ? 0 : 1;
	}
}

int main(int argc, char **argv)
{
	static const char *digits[12] = { "1", "2", "7", "0", "0", "0", "0", "0", "0", "0", "0", "1" };
	static char keys[12][48];
	long long t0;
	pid_t client;
	int i, st;

	if (argc < 2) {
		fprintf(stderr, "usage: %s gambatte_libretro.so\n", argv[0]);
		return 2;
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	g_opts[g_nopts][0] = "gambatte_gb_link_network_port"; g_opts[g_nopts++][1] = "56400";
	for (i = 0; i < 12; i++) {
		snprintf(keys[i], sizeof keys[i], "gambatte_gb_link_network_server_ip_%d", i + 1);
		g_opts[g_nopts][0] = keys[i];
		g_opts[g_nopts++][1] = digits[i];
	}
	g_opts[g_nopts][0] = "gambatte_gb_link_mode";
	t0 = now_ms();
	client = fork();
	if (client == 0) {
		g_role = "client";
		g_opts[g_nopts++][1] = "Network Client";
		usleep(300000);               /* the server listens first, as the host does */
		/* A to C, then D: the client's game unloads at 26 s */
		_exit(side(argv[1], false, t0, 26000, 28000));
	}
	g_role = "server";
	g_opts[g_nopts++][1] = "Network Server";
	if (fork() == 0) {
		/* B: the client stopped from 4 s to 17 s, then on again */
		usleep(4000 * 1000);
		kill(client, SIGSTOP);
		printf("------ %5lld client stopped\n", now_ms() - t0);
		usleep(13000 * 1000);
		kill(client, SIGCONT);
		printf("------ %5lld client on again\n", now_ms() - t0);
		_exit(0);
	}
	st = side(argv[1], true, t0, 0, 30000);
	{
		int cst = 0;

		waitpid(client, &cst, 0);
		wait(NULL);
		if (!WIFEXITED(cst) || WEXITSTATUS(cst)) st = 1;
	}
	printf("gblink-loopback: %s\n", st ? "FAILED" : "every phase holds");
	return st;
}
