/* SPDX-License-Identifier: GPL-3.0-only */
/* MP4's video picture of any shape, from the daemon's frames to the texture
 * the player draws it from:
 *
 *   make check-mp4size
 *
 * The launcher overwrites its video texture whole every frame, and after 8 of
 * those the Pixel 2's Mesa turns the texture from tiles into rows in place,
 * in the buffer it made for the tiles (src/mp4frame.h, mpf_stride). Rows are
 * 64-byte aligned, tiles only 16-pixel: a plane whose width is not a multiple
 * of 64 then runs past its buffer - a 4:3 video's 480, 720, 960 or 1440, as
 * the snaps that crashed did - and the launcher crashed, or the picture's
 * bottom edge came out green. So the daemon lays a picture's rows out
 * mpf_stride apart (a multiple of 128), and the texture is that wide.
 *
 * For each size here - 4:3 ones, 16:9 ones, odd, tiny, the largest, the size
 * changing inside one file - pictures are published as src/mp4d/vid.c
 * publishes them and read back by src/mp4video.c itself, built into this
 * check against a frames file of its own. The texture must be as wide as
 * the rows and as high as the picture, so each frame overwrites all of it;
 * each of its planes must fit Mesa's tile buffer as rows (the rule for an
 * 8-bit texture on Bifrost v7, in Mesa 25.0.7's pan_layout.c and the same in
 * 26.2.6's pan_mod.c, the device's);
 * mp4v_texture must say where the picture is; drawn from there, every pixel
 * is the picture's; mp4v_fit keeps its shape, its pixels' own counted; and
 * nothing may read or write out of bounds: built with UBSan where the
 * compiler has it, run under Guard Malloc on a Mac. Needs SDL2. */
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "../src/mp4frame.h"
#undef MPF_PATH
#define MPF_PATH MP4SIZE_FRAMES

#include "../src/mp4video.c"

static int failures;

#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); putchar('\n'); failures++; } } while (0)

/* mp4video.c's one need from the platform */
unsigned plat_now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (unsigned)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

/* An 8-bit plane of w x h on the Pixel 2's GPU fits its tile buffer as rows:
 * Mesa's u-interleaved layout is 16x16 tiles, rows of them 64-byte aligned;
 * its linear one, rows 64-byte aligned. Pages left aside: the buffer is
 * rounded up to one, and on that slack nothing can count. */
static uint32_t align(uint32_t n, uint32_t a) { return (n + a - 1) / a * a; }
static bool fits_as_rows(uint32_t w, uint32_t h)
{
	uint64_t tiled = (uint64_t)align(16 * align(w, 16), 64) * (align(h, 16) / 16);
	uint64_t linear = (uint64_t)align(w, 64) * h;

	return linear <= tiled;
}

static mpf_head *g_shm;
static uint32_t  g_serial;

static void pad(uint8_t *p, uint32_t stride, uint32_t w, uint32_t h, uint8_t v)
{
	uint32_t y;

	for (y = 0; y < h; y++) {
		memset(p + (size_t)y * stride, v, w);
		memset(p + (size_t)y * stride + w, v, stride - w);   /* vid.c: the row's last pixel */
	}
}

/* One picture of w x h, white (studio range), laid out as vid.c's publish()
 * lays it: Y rows mpf_stride(w) apart, U and V rows half that */
static void publish(uint32_t w, uint32_t h, uint32_t sar_num, uint32_t sar_den)
{
	int s = mpf_begin(g_shm);
	uint32_t st = mpf_stride(w), cst = mpf_chroma(st);
	uint8_t *y = mpf_slot_data(g_shm, s), *u = y + (size_t)st * h;

	pad(y, st, w, h, 235);
	pad(u, cst, mpf_chroma(w), mpf_chroma(h), 128);
	pad(u + (size_t)cst * mpf_chroma(h), cst, mpf_chroma(w), mpf_chroma(h), 128);
	g_shm->slot[s].w = w;
	g_shm->slot[s].h = h;
	g_shm->slot[s].stride = st;
	g_shm->slot[s].color = MPF_BT601;
	g_shm->slot[s].serial = ++g_serial;
	g_shm->slot[s].file = g_shm->file;
	g_shm->slot[s].pts = g_serial / 30.0;
	g_shm->sar_num = sar_num;
	g_shm->sar_den = sar_den;
	mpf_publish(g_shm, s);
}

/* The pixels of `r` on `surf` that are not white */
static long not_white(SDL_Surface *surf, SDL_Rect r)
{
	long bad = 0;
	int x, y;

	for (y = r.y; y < r.y + r.h; y++)
		for (x = r.x; x < r.x + r.w; x++) {
			Uint32 p = ((Uint32 *)((Uint8 *)surf->pixels + (size_t)y * surf->pitch))[x];
			Uint8 cr, cg, cb;

			SDL_GetRGB(p, surf->format, &cr, &cg, &cb);
			if (cr < 245 || cg < 245 || cb < 245) bad++;
		}
	return bad;
}

static void one_size(SDL_Renderer *r, SDL_Surface *surf, uint32_t w, uint32_t h,
                     uint32_t sar_num, uint32_t sar_den, bool new_file)
{
	SDL_Texture *t;
	SDL_Rect src = { -1, -1, -1, -1 }, box = { 0, 96, 1024, 576 }, fit;
	int xw = 0, xh = 0, k;
	double aspect;

	printf("  %ux%u%s\n", w, h, new_file ? "" : ", the same file");
	if (new_file) __atomic_store_n(&g_shm->file, g_shm->file + 1, __ATOMIC_RELEASE);
	__atomic_store_n(&g_shm->active, 1u, __ATOMIC_RELEASE);
	for (k = 0; k < 3; k++) {
		publish(w, h, sar_num, sar_den);
		CHECK(mp4v_update(r), "%ux%u: picture %d taken", w, h, k);
	}
	t = mp4v_texture(&src);
	CHECK(t != NULL, "%ux%u: a texture", w, h);
	if (!t) return;

	/* As wide as the rows and as high as the picture: each frame overwrites
	 * all of it, and each plane fits Mesa's tile buffer as rows */
	SDL_QueryTexture(t, NULL, NULL, &xw, &xh);
	CHECK(xw == (int)mpf_stride(w) && xh == (int)h && xw % 128 == 0,
	      "%ux%u: a texture of %dx%d, not the rows' %ux%u", w, h, xw, xh, mpf_stride(w), h);
	CHECK(fits_as_rows((uint32_t)xw, (uint32_t)xh) &&
	      fits_as_rows(mpf_chroma((uint32_t)xw), mpf_chroma((uint32_t)xh)),
	      "%ux%u: a %dx%d texture outgrows its tile buffer as rows", w, h, xw, xh);
	CHECK(src.x == 0 && src.y == 0 && src.w == (int)w && src.h == (int)h,
	      "%ux%u: drawn from %d,%d %dx%d", w, h, src.x, src.y, src.w, src.h);

	/* Drawn from that rectangle at its own size: all picture */
	SDL_SetRenderDrawColor(r, 255, 0, 0, 255);
	SDL_RenderClear(r);
	SDL_RenderCopy(r, t, &src, &(SDL_Rect){ 0, 0, (int)w, (int)h });
	SDL_RenderFlush(r);
	{
		long bad = not_white(surf, (SDL_Rect){ 0, 0, (int)w, (int)h });

		CHECK(bad == 0, "%ux%u: %ld pixels drawn from its rectangle are not the picture", w, h, bad);
	}

	/* Fitted to the player's box, in its own shape */
	fit = mp4v_fit(box);
	aspect = (double)w / h * sar_num / sar_den;
	CHECK(fit.w <= box.w && fit.h <= box.h && (fit.w == box.w || fit.h == box.h),
	      "%ux%u: fitted to %dx%d, inside %dx%d and touching it", w, h, fit.w, fit.h, box.w, box.h);
	CHECK(fit.w == box.w ? fabs(fit.h - box.w / aspect) <= 1 : fabs(fit.w - box.h * aspect) <= 1,
	      "%ux%u at %u:%u: fitted to %dx%d, not its shape", w, h, sar_num, sar_den, fit.w, fit.h);
}

int main(void)
{
	/* w, h, its pixels' shape, and whether it is a new file or the size
	 * changing inside one (a compilation joined without re-encoding) */
	static const struct { uint32_t w, h, sn, sd; bool file; } SIZES[] = {
		{ 1440, 1080, 1, 1, true }, { 480, 360, 1, 1, true },   { 320, 240, 1, 1, true },
		{ 640, 480, 1, 1, true },   { 720, 540, 1, 1, true },   { 960, 720, 1, 1, true },
		{ 720, 480, 8, 9, true },   { 704, 576, 12, 11, true }, { 720, 576, 16, 15, true },
		{ 640, 360, 1, 1, true },   { 854, 480, 1, 1, true },   { 426, 240, 1, 1, true },
		{ 1280, 720, 1, 1, true },  { 1920, 1080, 1, 1, true }, { 320, 480, 1, 1, true },
		{ 534, 480, 1, 1, true },   { 321, 241, 1, 1, true },   { 718, 539, 1, 1, true },
		{ 1, 1, 1, 1, true },       { 2, 2, 1, 1, true },       { 1, 480, 1, 1, true },
		{ 480, 1, 1, 1, true },     { 1919, 1087, 1, 1, true }, { 1450, 1088, 1, 1, true },
		{ MPF_MAX_W, MPF_MAX_H, 1, 1, true },
		/* one file, its pictures changing size: narrower inside one width
		 * of rows, wider out of it, and back */
		{ 720, 540, 1, 1, true },   { 700, 540, 1, 1, false },  { 480, 360, 1, 1, false },
		{ 321, 241, 1, 1, false },  { 1440, 1080, 1, 1, false }, { 480, 360, 1, 1, false },
		{ 640, 480, 1, 1, false },  { 2, 2, 1, 1, false },      { 1440, 1080, 1, 1, false },
	};
	SDL_Surface *surf;
	SDL_Renderer *r;
	size_t i;
	int fd;

	setvbuf(stdout, NULL, _IONBF, 0);
	/* The rule is the one that bit: a 1440x1080 texture of the picture's own
	 * width outgrows its buffer in both planes, 640x480's does not */
	CHECK(!fits_as_rows(1440, 1080) && !fits_as_rows(720, 540), "1440x1080 at its own width fits");
	CHECK(!fits_as_rows(480, 360) && !fits_as_rows(160, 240), "480x360 and a 320x480 snap fit");
	CHECK(fits_as_rows(640, 480) && fits_as_rows(320, 240), "640x480 does not fit");

	unlink(MP4SIZE_FRAMES);
	fd = open(MP4SIZE_FRAMES, O_RDWR | O_CREAT, 0644);
	if (fd < 0 || ftruncate(fd, MPF_SIZE) != 0) { printf("FAIL: no frames file %s\n", MP4SIZE_FRAMES); return 1; }
	g_shm = mmap(NULL, MPF_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (g_shm == MAP_FAILED) { printf("FAIL: mmap\n"); return 1; }
	g_shm->version = MPF_VERSION;
	g_shm->magic = MPF_MAGIC;

	if (SDL_Init(SDL_INIT_VIDEO) != 0) { printf("SDL: %s\n", SDL_GetError()); return 1; }
	surf = SDL_CreateRGBSurfaceWithFormat(0, MPF_MAX_W, MPF_MAX_H, 32, SDL_PIXELFORMAT_ARGB8888);
	r = surf ? SDL_CreateSoftwareRenderer(surf) : NULL;
	if (!r) { printf("FAIL: no renderer: %s\n", SDL_GetError()); return 1; }

	printf("MP4 pictures of every shape, read into their texture and drawn:\n");
	for (i = 0; i < sizeof SIZES / sizeof SIZES[0]; i++)
		one_size(r, surf, SIZES[i].w, SIZES[i].h, SIZES[i].sn, SIZES[i].sd, SIZES[i].file);
	mp4v_release();
	SDL_DestroyRenderer(r);
	SDL_FreeSurface(surf);
	munmap(g_shm, MPF_SIZE);
	unlink(MP4SIZE_FRAMES);
	if (failures) { printf("mp4size: %d FAILED\n", failures); return 1; }
	printf("ok: an MP4 picture of any shape goes to the GPU in a texture as wide as its rows, "
	       "a multiple of 128, that fits Mesa's tile buffer as rows, and is drawn whole\n");
	return 0;
}
