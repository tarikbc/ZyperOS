/* A video snap of any shape, from the daemon's frames to every place a snap
 * is drawn:
 *
 *   make check-snapsize
 *
 * A snap is whatever ScreenScraper encoded: 640x480 for most, but a Nintendo
 * DS game's is its two screens stacked (320x480), a Game Boy's 320x288, and
 * some are odd (534x480, 333x481, a width of 1). For each size, pictures are
 * published into a frames file exactly as src/muse/snap.c publishes them
 * (src/mp4frame.h), read back by src/snapview.c into its texture, and drawn
 * on the shelf (games_draw, the cover turning into the snap) and on a game's
 * Cover and Video page (mpg_draw, its video tile), in a software renderer,
 * the size changing between snaps as the shelf's does. The picture must keep
 * its shape inside the place it is drawn; its texture is never smaller than
 * 640x480 (mpf_tex_size: the Pixel 2's GPU faulted on narrower snaps, so a
 * smaller picture sits in a corner of one that size and is drawn from its
 * own rectangle); and nothing may read or write out of bounds: the check is
 * built with UBSan where the compiler has it (AddressSanitizer cannot load
 * beside the Mac's SDL2), and run under Guard Malloc on a Mac. Links the launcher's own objects, as tools/games-check.c
 * does, with src/snapview.c built here against a frames file of its own.
 */
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <SDL.h>
#include <SDL_image.h>

#include "../src/games.h"
#include "../src/home.h"
#include "../src/mediapage.h"
#include "../src/mp4frame.h"
#include "../src/snapframe.h"
#include "../src/snapview.h"
#include "../src/theme.h"
#include "../src/ui.h"

static int failures;

#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); putchar('\n'); failures++; } } while (0)

static mpf_head *g_shm;
static uint32_t  g_serial;

/* One picture of w x h, as src/muse/snap.c's publish() writes it: luma a
 * ramp, chroma grey, the last byte of each plane written */
static void publish(uint32_t w, uint32_t h, uint32_t file)
{
	int s = mpf_begin(g_shm);
	uint8_t *y = mpf_slot_data(g_shm, s), *u = y + (size_t)w * h;
	size_t i;

	for (i = 0; i < (size_t)w * h; i++) y[i] = (uint8_t)(i * 7);
	memset(u, 128, 2 * (size_t)mpf_chroma(w) * mpf_chroma(h));
	g_shm->slot[s].w = w;
	g_shm->slot[s].h = h;
	g_shm->slot[s].color = MPF_BT601;
	g_shm->slot[s].serial = ++g_serial;
	g_shm->slot[s].file = file;
	g_shm->slot[s].pts = 0;
	g_shm->sar_num = g_shm->sar_den = 1;
	mpf_publish(g_shm, s);
}

static SDL_Texture *g_cover;
static SDL_Texture *cover(void *ctx, int index, int *w, int *h, float *bottom)
{
	(void)ctx; (void)index;
	*w = 256; *h = 328;
	if (bottom) *bottom = 1;
	return g_cover;
}

static void one_size(SDL_Renderer *r, uint32_t w, uint32_t h)
{
	games_row row = { 0 };
	games_info info;
	mpg_page pg;
	SDL_Texture *t;
	int tw = 0, th = 0, xw = 0, xh = 0, k;
	SDL_Rect src = { 0, 0, 0, 0 };
	uint32_t file;

	printf("  %ux%u\n", w, h);
	/* The shelf asks, the daemon opens the file and puts out pictures */
	snapv_begin();
	file = g_shm->file + 1;
	__atomic_store_n(&g_shm->file, file, __ATOMIC_RELEASE);
	__atomic_store_n(&g_shm->active, 1u, __ATOMIC_RELEASE);
	for (k = 0; k < 4; k++) {
		publish(w, h, file);
		CHECK(snapv_pending(), "a new picture of %ux%u waits", w, h);
		CHECK(snapv_update(r), "and is taken");
	}
	t = snapv_texture(&tw, &th, &src);
	CHECK(t != NULL, "%ux%u: a texture", w, h);
	if (!t) return;
	CHECK(th == (int)h && abs(tw - (int)w) <= 1, "%ux%u: its shape %dx%d", w, h, tw, th);
	/* Never a texture smaller than 640x480 on the GPU (mpf_tex_size): the
	 * picture in its corner, drawn from its own rectangle */
	SDL_QueryTexture(t, NULL, NULL, &xw, &xh);
	CHECK(xw >= 640 && xh >= 480 && xw >= (int)w && xh >= (int)h,
	      "%ux%u: a texture of %dx%d, never under 640x480", w, h, xw, xh);
	CHECK(src.x == 0 && src.y == 0 && src.w == (int)w && src.h == (int)h,
	      "%ux%u: drawn from %d,%d %dx%d", w, h, src.x, src.y, src.w, src.h);
	{
		uint32_t pw, ph;

		mpf_tex_size(w, h, &pw, &ph);
		CHECK((int)pw == xw && (int)ph == xh, "%ux%u: the size mpf_tex_size says", w, h);
	}

	/* On the shelf, the cover turned into it, and half way */
	memset(&info, 0, sizeof info);
	info.system = "Nintendo DS";
	info.title = "Mario Kart DS";
	info.count = 3;
	info.cursor = 1;
	info.snap = t;
	info.snap_w = tw;
	info.snap_h = th;
	info.snap_src = src;
	games_reset(&row, 1, 3, 0);
	for (k = 0; k <= 2; k++) {
		SDL_FRect vr = games_cover_rect(tw, th, 0);

		info.snap_k = k / 2.0f;
		SDL_RenderClear(r);
		games_draw(&row, r, &info, cover, NULL, 1000, 0);
		CHECK(vr.w > 0 && vr.h > 0 && vr.w <= GAMES_COVER_W + 0.5f && vr.h <= GAMES_COVER_H + 0.5f,
		      "%ux%u on the shelf: %.1fx%.1f, inside %dx%d", w, h, vr.w, vr.h, GAMES_COVER_W, GAMES_COVER_H);
		CHECK(fabsf(vr.w / vr.h - (float)tw / th) < 0.02f, "%ux%u on the shelf keeps its shape", w, h);
	}

	/* On the Cover and Video page, its tile, playing and still */
	for (k = 0; k < 2; k++) {
		memset(&pg, 0, sizeof pg);
		pg.title = "Mario Kart DS";
		pg.video_head = "Video";
		pg.vid_tex = t;
		pg.vid_w = tw;
		pg.vid_h = th;
		pg.vid_src = src;
		pg.vid_disc = k == 0;
		pg.vid_load = -1;
		pg.row = k == 0 ? -1 : 0;
		pg.nrows = 1;
		pg.rows[0] = (mpg_row){ "Watch", "12 s", true };
		SDL_RenderClear(r);
		mpg_draw(r, &pg, 1000);
	}
}

int main(void)
{
	static const uint32_t SIZES[][2] = {
		{ 640, 480 }, { 320, 480 }, { 534, 480 }, { 720, 480 }, { 320, 288 }, { 160, 144 },
		{ 333, 481 }, { 1, 480 }, { 480, 1 }, { 2, 2 }, { 256, 384 }, { 640, 480 }, { 320, 480 },
		{ MPF_MAX_W, MPF_MAX_H }, { 90, 1088 },
	};
	SDL_Surface *screen;
	SDL_Renderer *r;
	int fd;
	size_t i;

	setvbuf(stdout, NULL, _IONBF, 0);
	unlink(SNAP_FRAMES);
	fd = open(SNAP_FRAMES, O_RDWR | O_CREAT, 0644);
	if (fd < 0 || ftruncate(fd, MPF_SIZE) != 0) { printf("FAIL: no frames file %s\n", SNAP_FRAMES); return 1; }
	g_shm = mmap(NULL, MPF_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (g_shm == MAP_FAILED) { printf("FAIL: mmap\n"); return 1; }
	g_shm->version = MPF_VERSION;
	g_shm->magic = MPF_MAGIC;

	if (SDL_Init(SDL_INIT_VIDEO) != 0) { printf("SDL: %s\n", SDL_GetError()); return 1; }
	IMG_Init(IMG_INIT_PNG);
	screen = SDL_CreateRGBSurfaceWithFormat(0, 1024, 768, 32, SDL_PIXELFORMAT_ARGB8888);
	r = SDL_CreateSoftwareRenderer(screen);
	theme_set_file(NULL);
	theme_init(APPEAR_LIGHT);
	if (!r || !ui_init(r, "res/fonts/menu.ttf") || !page_fonts_init("res/fonts/menu.ttf")) {
		printf("FAIL: no renderer or fonts\n");
		return 1;
	}
	g_cover = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 256, 328);

	printf("snaps of every shape, read and drawn:\n");
	for (i = 0; i < sizeof SIZES / sizeof SIZES[0]; i++) one_size(r, SIZES[i][0], SIZES[i][1]);
	snapv_release();
	munmap(g_shm, MPF_SIZE);
	unlink(SNAP_FRAMES);
	if (failures) { printf("snapsize: %d FAILED\n", failures); return 1; }
	printf("ok: a snap of any shape is read whole and drawn in its own shape, on the shelf and on its page\n");
	return 0;
}
