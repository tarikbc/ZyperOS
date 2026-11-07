/* SPDX-License-Identifier: GPL-3.0-only */
/* Finished pictures put on screen a few a frame (src/putpace.h,
 * src/texload.h's texload_deliver), on the launcher's own objects.
 *
 * The rules first, with the costs handed in, so they hold exactly: the first
 * picture of a frame always goes in, so a slow device still fills its shelf;
 * the rest while they fit in the frame's share; the guess believes a slow
 * picture at once and a fast one slowly; a still counts as a picture, not as
 * a few pixels; and a frame never spends more than its share and the last
 * guess's error, or one picture when one picture is more than the share.
 *
 * Then the art workers themselves, with real cards and real textures on
 * SDL's software renderer: a shelf and more finished at once and handed to
 * the frame, every frame's time on them inside the bound - and the bound
 * held with uploads made slow on purpose, as a slow GPU's are; every picture
 * in its own slot and none lost, in a few frames; a loop that draws only
 * when something changed drawing until the last one is in, with nothing
 * pressed; the one picture a screen is waiting for taken out of its turn;
 * and the workers waiting for room rather than throwing a finished picture
 * away when more are finished than the frame has taken.
 *
 * SDL_VIDEODRIVER=dummy, no window.
 */
#include "putpace.h"
#include "texload.h"
#include "theme.h"
#include "ui.h"

#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

#define CARD_PX (512L * 656L)

/* ---- the rules, with the costs handed in ---------------------------------- */

/* One frame of pictures of `px` pixels each costing `ms_per_mpx` a megapixel,
 * `waiting` of them: how many go in, and what the frame spends. */
static int frame_of(putpace *p, long px, float ms_per_mpx, int waiting,
                    float *spent)
{
	int n = 0;

	putpace_frame(p);
	while (n < waiting && putpace_room(p, px)) {
		putpace_took(p, px, ms_per_mpx * (float)px / 1e6f);
		n++;
	}
	*spent = p->spent;
	return n;
}

static void the_rules(void)
{
	putpace p;
	float spent;
	int n, total, frames;

	putpace_init(&p, 4.0f);
	ck(putpace_guess(&p, CARD_PX) == 0.0f, "nothing is guessed before the first is timed");
	ck(putpace_room(&p, CARD_PX), "the first of a frame goes in");

	/* A card costing 1.5 ms: two a frame in 4, since the third would make 4.5 */
	n = frame_of(&p, CARD_PX, 1.5f / (CARD_PX / 1e6f), 30, &spent);
	ck(n == 2 && fabsf(spent - 3.0f) < 1e-4f, "1.5 ms cards: two a frame in a 4 ms share");
	/* Six a frame on the host's software renderer (0.04 ms) would be a hundred */
	putpace_init(&p, 4.0f);
	n = frame_of(&p, CARD_PX, 0.04f / (CARD_PX / 1e6f), 11, &spent);
	ck(n == 11 && spent < 0.5f, "a shelf of 0.04 ms uploads all in one frame");

	/* One picture more than the share: one a frame, and every frame one */
	putpace_init(&p, 4.0f);
	for (frames = 0, total = 0; total < 11 && frames < 100; frames++) {
		n = frame_of(&p, CARD_PX, 6.0f / (CARD_PX / 1e6f), 11 - total, &spent);
		ck(n == 1, "a 6 ms upload: one a frame, never none");
		total += n;
	}
	ck(frames == 11, "a 6 ms upload: the shelf in as many frames as cards");

	/* Up at once, down an eighth of the way */
	putpace_init(&p, 4.0f);
	putpace_took(&p, CARD_PX, 1.0f);
	ck(fabsf(putpace_guess(&p, CARD_PX) - 1.0f) < 1e-4f, "the first timing is the guess");
	putpace_took(&p, CARD_PX, 3.0f);
	ck(fabsf(putpace_guess(&p, CARD_PX) - 3.0f) < 1e-4f, "a slower one is believed at once");
	putpace_took(&p, CARD_PX, 1.0f);
	ck(fabsf(putpace_guess(&p, CARD_PX) - 2.75f) < 1e-4f, "a faster one an eighth of the way");
	ck(fabsf(putpace_guess(&p, 2 * CARD_PX) - 5.5f) < 1e-3f, "twice the pixels, twice the guess");
	ck(putpace_guess(&p, 0) == 0.0f, "no picture, no guess");

	/* A 128 x 128 still at 0.1 ms taught as a still, not as 16384 pixels */
	putpace_init(&p, 4.0f);
	putpace_took(&p, 128L * 128L, 0.1f);
	ck(putpace_guess(&p, CARD_PX) < 0.6f,
	   "a still's cost says a card costs about five stills, not twenty");
	ck(putpace_guess(&p, 128L * 128L) > 0.099f, "and a still what a still does");

	/* Pictures of every size, a cost a megapixel with a little scatter: no
	 * frame past its share by more than one picture's error, and every frame
	 * at least one in. */
	{
		static const long sizes[] = { 128L * 128L, 512L * 656L, 512L * 512L,
		                              359L * 512L, 1024L * 1024L, 64L * 64L };
		unsigned seed = 7;
		int left = 400, worst_frames = 0, frames = 0;

		putpace_init(&p, 4.0f);
		/* A frame that puts none in would be this loop forever: 1000 frames
		 * are far more than 400 pictures need even one a frame */
		while (left > 0 && frames++ < 1000) {
			float frame_spent = 0.0f, last = 0.0f;
			int k = 0;

			putpace_frame(&p);
			for (;;) {
				long px;
				float cost;

				seed = seed * 1103515245u + 12345u;
				px = sizes[(seed >> 16) % 6];
				if (!putpace_room(&p, px) || !left) break;
				/* 5 ms a megapixel, give or take a tenth */
				cost = 5.0f * (float)(px < PUTPACE_LEAST_PX ? PUTPACE_LEAST_PX : px) / 1e6f *
				       (0.9f + 0.2f * (float)((seed >> 8) & 255) / 255.0f);
				putpace_took(&p, px, cost);
				frame_spent += cost;
				last = cost;
				k++;
				left--;
			}
			if (!k && left) ck(0, "a frame with pictures waiting put none in");
			if (k > 1 && frame_spent - last > 4.0f + 1e-4f)
				ck(0, "a frame spent its share before its last picture");
			if (k > 1 && frame_spent > 4.0f * 1.25f) worst_frames++;
		}
		ck(worst_frames == 0, "no frame went a quarter past its share on a guess");
		ck(left == 0, "four hundred pictures of every size all went in");
	}
}

/* ---- the art workers ------------------------------------------------------ */

#define SHELF 40     /* more than the ring holds: the workers wait for room */
#define SYS 7

static SDL_Renderer *R;
static SDL_Texture *slot[SHELF];
static int landed_twice, landed_wrong;
static float slow_ms;          /* each upload made this slow, on purpose */
static double put_ms[64];      /* this frame's, one a picture */
static int nput;

static double now_ms(void)
{
	return (double)SDL_GetPerformanceCounter() * 1000.0 /
	       (double)SDL_GetPerformanceFrequency();
}

/* What the shelf's put does with a card (src/main.c's shelf_put): its
 * texture, in its slot - and a check that the card is this slot's, by its
 * pixels' hash against the frame's own card for the slot's title. */
static Uint64 want_hash[SHELF];

static Uint64 pixels_hash(const SDL_Surface *s)
{
	Uint64 h = 1469598103934665603ull;

	for (int y = 0; y < s->h; y++) {
		const Uint8 *row = (const Uint8 *)s->pixels + (size_t)y * s->pitch;

		for (int x = 0; x < s->w * 4; x++) h = (h ^ row[x]) * 1099511628211ull;
	}
	return h;
}

/* The surface each slot was handed, kept to be told apart from the others
 * once the frames are over: hashing it in `put` would be the frame's time */
static SDL_Surface *kept[SHELF];

static void slow_put(void *ctx, texload_got *g)
{
	double t0 = now_ms();

	(void)ctx;
	if (g->sys != SYS || g->idx < 0 || g->idx >= SHELF || !g->surf || !g->card) {
		landed_wrong++;
		if (g->surf) SDL_FreeSurface(g->surf);
	} else if (slot[g->idx]) {
		landed_twice++;
		SDL_FreeSurface(g->surf);
	} else {
		slot[g->idx] = ui_card_upload(R, g->surf, g->theme);
		kept[g->idx] = g->surf;
	}
	while (now_ms() - t0 < slow_ms) { }
	if (nput < 64) put_ms[nput] = now_ms() - t0;
	nput++;
}

/* Each slot's card the frame's own card for that slot's title */
static bool all_their_own(void)
{
	for (int i = 0; i < SHELF; i++)
		if (kept[i] && pixels_hash(kept[i]) != want_hash[i]) return false;
	return true;
}

static void clear_shelf(void)
{
	for (int i = 0; i < SHELF; i++) {
		if (slot[i]) SDL_DestroyTexture(slot[i]);
		if (kept[i]) SDL_FreeSurface(kept[i]);
		slot[i] = NULL;
		kept[i] = NULL;
	}
	landed_twice = landed_wrong = 0;
}

static void title_of(int i, char *out, size_t n)
{
	/* Titles of different widths, so a card in the wrong slot shows */
	static const char *const w[] = { "I", "Ox", "Kid", "Zelda", "Contra", "Gradius",
	                                 "Metroid", "Castlevania" };
	snprintf(out, n, "%s %s", w[i % 8], w[(i / 8) % 8]);
}

static void ask_shelf(int from, int to)
{
	for (int i = from; i < to; i++) {
		ui_card_order o;
		char t[64];

		title_of(i, t, sizeof t);
		ui_card_order_set(&o, t, 0x3D7EE0, false);
		texload_want_or_draw(SYS, i, "/no/such/.media/x.png", NULL, &o);
	}
}

/* What each slot's card is, from the frame's own */
static void learn_cards(void)
{
	for (int i = 0; i < SHELF; i++) {
		ui_card_order o;
		char t[64];
		SDL_Surface *s;

		title_of(i, t, sizeof t);
		ui_card_order_set(&o, t, 0x3D7EE0, false);
		s = ui_card_paint(&o, false);
		want_hash[i] = s ? pixels_hash(s) : 0;
		if (s) SDL_FreeSurface(s);
	}
}

static int filled(void)
{
	int n = 0;

	for (int i = 0; i < SHELF; i++) n += slot[i] != NULL;
	return n;
}

/* Everything asked for, finished and waiting - the ring's worth, and the two
 * workers holding one each, waiting for room - before the frames start */
static bool wait_finished(int want_waiting)
{
	Uint32 until = SDL_GetTicks() + 5000;

	while (SDL_GetTicks() < until) {
		if (texload_waiting() >= want_waiting) return true;
		SDL_Delay(2);
	}
	return false;
}

/* Frames until `want` slots are full: each frame's time on the pictures,
 * the longest upload, and the most a frame spent before its last picture,
 * which is what the share bounds. Returns the frames it took. */
static int run_frames(int want, int max_frames, double *worst_frame,
                      double *worst_before_last, double *worst_put)
{
	int frames = 0;

	*worst_frame = *worst_before_last = *worst_put = 0.0;
	while (filled() < want && frames < max_frames) {
		double t0, t, before = 0.0;
		int n, waiting = texload_waiting();

		nput = 0;
		t0 = now_ms();
		n = texload_deliver(theme_index(), slow_put, NULL);
		t = now_ms() - t0;
		frames++;
		if (!waiting && !n) {
			/* Not finished yet: the workers are drawing the rest */
			SDL_Delay(1);
			continue;
		}
		if (waiting) ck(n >= 1, "a frame with pictures waiting put at least one in");
		for (int k = 0; k + 1 < nput && k < 64; k++) before += put_ms[k];
		for (int k = 0; k < nput && k < 64; k++)
			if (put_ms[k] > *worst_put) *worst_put = put_ms[k];
		if (t > *worst_frame) *worst_frame = t;
		if (nput > 1 && before > *worst_before_last) *worst_before_last = before;
		/* A loop that draws only when something changed: nothing pressed,
		 * and pictures still waiting say so */
		if (filled() < want && texload_waiting() > 0)
			ck(texload_ready(), "pictures left waiting keep the loop drawing");
	}
	return frames;
}

static void the_workers(void)
{
	double worst, before_last, worst_put;
	int frames;

	learn_cards();

	/* At the host's own speed: a shelf and more, all finished at once */
	printf("  at the host's speed\n");
	slow_ms = 0.0f;
	clear_shelf();
	ask_shelf(0, 24);
	ck(wait_finished(24), "two dozen finished");
	ask_shelf(24, SHELF);       /* the ring is full: these wait for room */
	SDL_Delay(50);
	frames = run_frames(SHELF, 400, &worst, &before_last, &worst_put);
	ck(filled() == SHELF && !landed_twice && !landed_wrong && all_their_own(),
	   "every picture in its own slot, once, and none lost");
	ck(frames <= 30, "the shelf in a few frames");
	ck(before_last <= TEXLOAD_SHARE_MS + 1.0,
	   "no frame took another picture once its share was spent");
	printf("    %d pictures in %d frames, the longest %.2f ms (share %.1f)\n",
	       SHELF, frames, worst, (double)TEXLOAD_SHARE_MS);

	/* Uploads made 1.5 ms each, as a slow GPU's are: two a frame */
	printf("  with each upload 1.5 ms\n");
	slow_ms = 1.5f;
	clear_shelf();
	ask_shelf(0, 24);
	ck(wait_finished(24), "two dozen finished");
	frames = run_frames(24, 400, &worst, &before_last, &worst_put);
	ck(filled() == 24 && !landed_twice && !landed_wrong && all_their_own(),
	   "every picture in its own slot, once, and none lost");
	ck(before_last <= TEXLOAD_SHARE_MS + 1.0,
	   "a slow upload: no frame took another picture past its share");
	ck(worst <= TEXLOAD_SHARE_MS + worst_put + 1.0,
	   "a slow upload: a frame is its share and one picture at most");
	printf("    24 pictures in %d frames, the longest %.2f ms, the longest upload %.2f ms\n",
	       frames, worst, worst_put);

	/* Each upload more than the whole share: one a frame, and every frame one */
	printf("  with each upload 6 ms\n");
	slow_ms = 6.0f;
	clear_shelf();
	ask_shelf(0, 12);
	ck(wait_finished(12), "a dozen finished");
	frames = run_frames(12, 400, &worst, &before_last, &worst_put);
	ck(filled() == 12 && !landed_twice && !landed_wrong && all_their_own(),
	   "every picture in its own slot, once, and none lost");
	ck(before_last == 0.0, "an upload past the share: one picture a frame");
	ck(frames <= 14, "and the dozen in about a dozen frames, not stopped");
	printf("    12 pictures in %d frames, the longest %.2f ms\n", frames, worst);

	/* The shelf's loop with nothing pressed (src/main.c's main loop): it
	 * draws a frame only when texload_ready says pictures are waiting, and
	 * otherwise rests, as plat_input_wait does, until the workers are idle.
	 * Two a frame here, so most frames leave some waiting, and the loop has
	 * to keep drawing for them with no press to wake it. */
	printf("  a loop that draws only when something changed\n");
	slow_ms = 1.5f;
	clear_shelf();
	ask_shelf(0, 24);
	{
		Uint32 until = SDL_GetTicks() + 5000;
		int drawn = 0;

		while (SDL_GetTicks() < until) {
			if (texload_ready()) {
				texload_deliver(theme_index(), slow_put, NULL);
				drawn++;
			} else if (texload_idle()) {
				break;
			} else {
				SDL_Delay(4);
			}
		}
		ck(filled() == 24 && !landed_twice && !landed_wrong && all_their_own(),
		   "with nothing pressed, the loop drew until the last picture was in");
		ck(drawn >= 12, "and drew a frame for every two of them");
		printf("    24 pictures in %d frames drawn, nothing pressed\n", drawn);
	}

	/* The one a screen waits for, out of its turn */
	printf("  the one picture a screen is waiting for\n");
	slow_ms = 0.0f;
	clear_shelf();
	ask_shelf(0, 20);
	ck(wait_finished(20), "a score finished");
	nput = 0;
	ck(texload_deliver_slot(SYS, 0, theme_index(), slow_put, NULL) && slot[0] && nput == 1,
	   "the one asked for, wherever it is in line, handed over alone");
	ck(texload_waiting() == 19, "and the rest left in line");
	ck(!texload_deliver_slot(SYS, 0, theme_index(), slow_put, NULL),
	   "and asked again, nothing: it is in");
	run_frames(20, 100, &worst, &before_last, &worst_put);
	ck(filled() == 20 && !landed_twice && !landed_wrong && all_their_own(), "and the rest after it, each once");
	texload_bump();
	clear_shelf();
}

int main(void)
{
	SDL_Surface *screen;

	/* Said as it goes: a check that crashes says how far it got */
	setvbuf(stdout, NULL, _IONBF, 0);
	printf("the rules:\n");
	the_rules();

	if (SDL_Init(SDL_INIT_VIDEO) != 0) { printf("SDL: %s\n", SDL_GetError()); return 1; }
	IMG_Init(IMG_INIT_PNG);
	screen = SDL_CreateRGBSurfaceWithFormat(0, 1024, 768, 32, SDL_PIXELFORMAT_ARGB8888);
	R = SDL_CreateSoftwareRenderer(screen);
	theme_set_file(NULL);
	theme_init(APPEAR_LIGHT);
	if (!ui_init(R, "res/fonts/menu.ttf")) { printf("FAIL ui_init\n"); return 1; }
	if (!texload_start()) { printf("FAIL texload_start\n"); return 1; }
	printf("the art workers:\n");
	the_workers();
	texload_stop();
	ui_quit();
	SDL_DestroyRenderer(R);
	SDL_FreeSurface(screen);
	IMG_Quit();
	SDL_Quit();
	if (fails) { printf("putpace: %d FAILED\n", fails); return 1; }
	printf("ok: finished pictures a few a frame, inside the frame's share or one picture, "
	       "every one in its slot in a few frames, the loop drawing until the last\n");
	return 0;
}
