/* SPDX-License-Identifier: GPL-3.0-only */
/* Frames into a player 2 share file, as Diatom writes them in a game, for
 * p2d's checks (tools/p2d-check.py, and the play page's).
 *
 *     p2d-feed --share PATH [--fps 60] [--rgba FILE] [--frames N]
 *
 * The file is made as Diatom makes it (diatom/src/player2.c): a new file of
 * P2S_SIZE, the version and then the magic written last, so p2d never sees a
 * half-made one. Then a frame every 1/fps s, with p2s_write.
 *
 * Without --rgba the frames are drawn: 256x240, a few colors and a box that
 * moves, so each delta is small, as an NES game's is. With --rgba, FILE's
 * frames in order and round again: 256x240 each, four bytes a pixel in R, G,
 * B, 255 order (the codec check's build-native/p2codec-fixture.rgba), the
 * k-th of them, from 0, written as the share's frame k+1. So the share's
 * frame n shows the file's frame (n-1) % count, which is what the play
 * page's check compares the canvas with.
 *
 * --frames N stops after N frames and keeps the share as it is, the picture
 * holding still for a check to look at. SIGTERM (or SIGINT) clears the magic
 * and removes the file, as Diatom does when a game ends. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../src/p2share.h"

#define W 256
#define H 240

static volatile sig_atomic_t g_term;

static void on_term(int sig)
{
	(void)sig;
	g_term = 1;
}

static void usage(void)
{
	fprintf(stderr, "usage: p2d-feed --share PATH [--fps 60] [--rgba FILE] [--frames N]\n");
	exit(2);
}

static long long now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* A few colors in stripes and a 24-pixel box moving across, the k-th */
static void draw(uint32_t *px, unsigned long k)
{
	static const uint32_t pal[] = { 0x000000, 0x5c94fc, 0x00a800, 0xfcfcfc, 0xd82800, 0xfca044 };
	unsigned bx = (unsigned)(k * 2 % (W - 24)), by = 60 + (unsigned)(k / ((W - 24) / 2) % 8) * 16;

	for (unsigned y = 0; y < H; y++)
		for (unsigned x = 0; x < W; x++) {
			uint32_t c = pal[(y / 40) % 4];

			if (x >= bx && x < bx + 24 && y >= by && y < by + 24) c = pal[4 + (k / 30) % 2];
			px[y * W + x] = c;
		}
}

/* FILE's frames as XRGB8888, in place; their count, or 0 */
static uint32_t *load_rgba(const char *path, unsigned long *count)
{
	const size_t frame = (size_t)W * H * 4;
	struct stat st;
	uint8_t *buf;
	FILE *f = fopen(path, "rb");

	if (!f || fstat(fileno(f), &st) != 0 || st.st_size <= 0 || (size_t)st.st_size % frame) {
		fprintf(stderr, "p2d-feed: %s is not %dx%d RGBA frames\n", path, W, H);
		if (f) fclose(f);
		return NULL;
	}
	buf = malloc((size_t)st.st_size);
	if (!buf || fread(buf, 1, (size_t)st.st_size, f) != (size_t)st.st_size) {
		fprintf(stderr, "p2d-feed: cannot read %s\n", path);
		fclose(f);
		free(buf);
		return NULL;
	}
	fclose(f);
	/* R, G, B, 255 to 0x00RRGGBB: the same four bytes, so in place */
	for (size_t i = 0; i < (size_t)st.st_size; i += 4) {
		uint32_t v = (uint32_t)buf[i] << 16 | (uint32_t)buf[i + 1] << 8 | buf[i + 2];

		memcpy(buf + i, &v, 4);
	}
	*count = (unsigned long)((size_t)st.st_size / frame);
	return (uint32_t *)(void *)buf;
}

/* The share, made as Diatom makes it */
static p2s_head *share_make(const char *path)
{
	p2s_head *hd;
	void *base;
	int fd;

	unlink(path);
	fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	if (fd < 0 || ftruncate(fd, P2S_SIZE) != 0) {
		fprintf(stderr, "p2d-feed: cannot make %s: %s\n", path, strerror(errno));
		if (fd >= 0) close(fd);
		unlink(path);
		return NULL;
	}
	base = mmap(NULL, P2S_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (base == MAP_FAILED) {
		fprintf(stderr, "p2d-feed: cannot map %s: %s\n", path, strerror(errno));
		unlink(path);
		return NULL;
	}
	hd = base;
	__atomic_store_n(&hd->version, P2S_VERSION, __ATOMIC_RELAXED);
	__atomic_store_n(&hd->magic, P2S_MAGIC, __ATOMIC_RELEASE);
	return hd;
}

int main(int argc, char **argv)
{
	const char *share = NULL, *rgba = NULL;
	long fps = 60, frames = -1;
	unsigned long count = 0, k;
	uint32_t *fixture = NULL, *drawn = NULL;
	struct sigaction sa;
	long long next, period;
	p2s_head *hd;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--share") && i + 1 < argc) share = argv[++i];
		else if (!strcmp(argv[i], "--fps") && i + 1 < argc) fps = strtol(argv[++i], NULL, 10);
		else if (!strcmp(argv[i], "--rgba") && i + 1 < argc) rgba = argv[++i];
		else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = strtol(argv[++i], NULL, 10);
		else usage();
	}
	if (!share || fps <= 0 || fps > 1000) usage();
	if (rgba && !(fixture = load_rgba(rgba, &count))) return 1;
	if (!rgba && !(drawn = malloc((size_t)W * H * 4))) return 1;

	/* No SA_RESTART: the sleep between frames ends at the signal */
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_term;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	if (!(hd = share_make(share))) return 1;
	period = 1000000000LL / fps;
	next = now_ns();
	for (k = 0; !g_term && (frames < 0 || k < (unsigned long)frames); k++) {
		const uint32_t *px;
		long long now;

		if (fixture) {
			px = fixture + (k % count) * (size_t)W * H;
		} else {
			draw(drawn, k);
			px = drawn;
		}
		p2s_write(hd, px, W, H, (size_t)W * 4, 1);

		/* On a schedule, not a sleep after each frame, so the rate is the
		 * rate; a feed far behind (a stopped process) starts over from now
		 * rather than racing to catch up */
		next += period;
		now = now_ns();
		if (now > next + period) next = now;
		if (now < next) {
			struct timespec ts = { (time_t)((next - now) / 1000000000LL),
			                       (long)((next - now) % 1000000000LL) };

			nanosleep(&ts, NULL);
		}
	}
	/* A sleep, not pause(): a signal between the test and the pause would
	 * be missed and the feed would wait for ever */
	while (!g_term) {
		struct timespec ts = { 0, 50000000L };

		nanosleep(&ts, NULL);
	}

	__atomic_store_n(&hd->magic, 0u, __ATOMIC_RELEASE);
	munmap(hd, P2S_SIZE);
	unlink(share);
	free(fixture);
	free(drawn);
	return 0;
}
