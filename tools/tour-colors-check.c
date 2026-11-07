/* SPDX-License-Identifier: GPL-3.0-only */
/* A tutorial frame drawn after its fade must have the same colors as that
 * frame drawn first. A single --shot never warms the text cache with a low
 * alpha, which hid the first-start tutorial's unreadable text on both themes.
 * This also turns the theme with cached details and bars still alive. */
#include "ui.h"
#include "tourdraw.h"
#include <stdio.h>
#include <string.h>

#define W 1024
#define H 768
static SDL_Renderer *renderer;
static Uint32 expected[W * H], actual[W * H];

/* The only platform service tourdraw needs, when it builds its details
 * texture. The software renderer's screen is its original surface. */
void plat_draw_to_screen(void) { SDL_SetRenderTarget(renderer, NULL); }

static int read_frame(const tour *t, Uint32 now, Uint32 *pixels)
{
	tour_draw(renderer, t, now, NULL);
	return SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
	                            pixels, W * 4) == 0;
}

static int open_ui(int th)
{
	theme_init(th == DS_LIGHT ? APPEAR_LIGHT : APPEAR_DARK);
	return ui_init(renderer, "res/fonts/menu.ttf");
}

static void close_ui(void)
{
	tour_draw_free();
	ui_quit();
}

int main(void)
{
	SDL_Surface *surface;
	int th, step, failures = 0;
	const Uint32 end = 10000;

	if (SDL_Init(0) != 0) return 1;
	surface = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
	renderer = surface ? SDL_CreateSoftwareRenderer(surface) : NULL;
	if (!renderer) { fprintf(stderr, "tour-colors: %s\n", SDL_GetError()); return 1; }
	theme_set_file(NULL);
	for (th = 0; th < DS_THEMES; th++) {
		for (step = TOUR_WELCOME; step <= TOUR_FINISH; step++) {
			tour t;
			Uint32 at;

			tour_pose(&t, (tour_step)step, end, 2000, -1, true);
			if (!open_ui(th) || !read_frame(&t, end, expected)) return 1;
			close_ui();
			if (!open_ui(th)) return 1;
			for (at = end - 2000; at < end; at += 33)
				tour_draw(renderer, &t, at, NULL);
			if (!read_frame(&t, end, actual)) return 1;
			if (memcmp(actual, expected, sizeof actual)) {
				fprintf(stderr, "FAIL theme %d step %d: colors depend on earlier fade frames\n", th, step);
				failures++;
			}
			/* Keep the cached textures, change theme, then change back. */
			theme_init(th == DS_LIGHT ? APPEAR_DARK : APPEAR_LIGHT);
			tour_draw(renderer, &t, end, NULL);
			theme_init(th == DS_LIGHT ? APPEAR_LIGHT : APPEAR_DARK);
			if (!read_frame(&t, end, actual)) return 1;
			if (memcmp(actual, expected, sizeof actual)) {
				fprintf(stderr, "FAIL theme %d step %d: colors stale after theme change\n", th, step);
				failures++;
			}
			close_ui();
		}
	}
	SDL_DestroyRenderer(renderer);
	SDL_FreeSurface(surface);
	SDL_Quit();
	if (!failures) puts("ok: every tutorial frame keeps its colors after a fade and a theme change");
	return failures != 0;
}
