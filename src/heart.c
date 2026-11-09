/* SPDX-License-Identifier: GPL-3.0-only */
/* draw_heart, apart from src/zyperdraw.c so the games page (src/games.c)
 * and the checks that link it without the launcher's main can draw the
 * favorite's heart. Declared in src/zyperdraw.h. */
#include <math.h>
#include <SDL.h>

#include "zyperdraw.h"

/* A heart, as a fan over the classic parametric curve:
 *
 *     x = 16 sin^3 t
 *     y = 13 cos t - 5 cos 2t - 2 cos 3t - cos 4t
 *
 * Mirrors markdef.heart(), which is the definition. The Favorites card's
 * heart is the same curve, drawn from it once by the script that drew that
 * card. The same curve still marks favorite games: change the curve and that
 * mark is redrawn by hand to match. The zyper mark no longer needs this,
 * since src/zypermark.h is generated and checked (see draw_zyper).
 *
 * Fanned from a point BELOW the center rather than from the center itself. The
 * notch between the lobes is the one concave part of the shape, and a fan
 * apex level with it produces slivers that cross the notch and fill it in.
 * Dropping the apex puts every boundary point in view of it. A fan over
 * SDL_RenderGeometry because SDL has no polygon fill; it antialiases nothing
 * and so looks identical to the generators' rasterizer.
 */
#define HEART_N 48

void draw_heart(SDL_Renderer *r, float cx, float cy, float rad,
                SDL_Color col)
{
	SDL_Vertex v[HEART_N + 1];
	int idx[HEART_N * 3], i;

	v[0].position.x = cx;
	v[0].position.y = cy + rad * 0.25f;
	for (i = 0; i < HEART_N; i++) {
		float t = (float)(2.0 * M_PI * i / HEART_N);
		float x = 16.0f * powf(sinf(t), 3.0f);
		float y = 13.0f * cosf(t) - 5.0f * cosf(2 * t)
		          - 2.0f * cosf(3 * t) - cosf(4 * t);

		/* The curve spans 32 wide and 30 tall; halving the wider axis makes
		 * `rad` a radius in the same sense the star's was. */
		v[i + 1].position.x = cx + x * rad / 16.0f;
		v[i + 1].position.y = cy - y * rad / 16.0f;
	}
	for (i = 0; i <= HEART_N; i++) {
		v[i].color = col;
		v[i].tex_coord.x = v[i].tex_coord.y = 0;
	}
	for (i = 0; i < HEART_N; i++) {
		idx[i * 3 + 0] = 0;
		idx[i * 3 + 1] = 1 + i;
		idx[i * 3 + 2] = 1 + (i + 1) % HEART_N;
	}
	SDL_RenderGeometry(r, NULL, v, HEART_N + 1, idx, HEART_N * 3);
}
