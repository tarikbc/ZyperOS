/* SPDX-License-Identifier: GPL-3.0-only */
/* The handhelds' models (src/device/td_model.h): every key once, the GKD
 * Pixel 2's places, views and kept boxes as src/tourdevice.c drew them
 * before they were data (its FACE[], DPAD_*, MENU_*, POWER_*, VOL*_Z,
 * SHOULDER[], SH_TOP, the FRONT/RIGHT/LEFT/BOTH/TOP poses, the tutorial's
 * 304 x 382 box and the Controls tab's 256 x 332, 3.15 units a millimetre
 * turned 10 degrees), so the drawing that reads them draws the same pixels;
 * and the TrimUI Brick's as src/device/brick_model.c measures them: its outline, its keys on their faces at
 * their places and sizes, the shoulders on the back ledge, and the back
 * view that shows them; the TrimUI Smart Pro's (src/device/smartpro_model.c);
 * and the R36S's (src/device/r36s_model.c). No SDL. */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "../src/device/td_model.h"
#include "../src/design.h"

static int fails, checks;

static void ck(int cond, const char *what)
{
	checks++;
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

static int is(const td_model *m, td_key k, td_face f, float x, float z, float r)
{
	const td_part *p = td_part_of(m, k);

	return p && p->face == f && p->x == x && p->z == z && p->r == r;
}

static int view_is(td_view v, float deg, float s, float pitch)
{
	return v.deg == deg && v.s == s && v.pitch == pitch;
}

/* Every part of `m` a key of the handheld's, each once */
static void keys_once(const td_model *m)
{
	int i, j;

	for (i = 0; i < m->n_parts; i++) {
		ck(m->parts[i].key >= 0 && m->parts[i].key < TD_KEYS, "each part a key of the handheld");
		for (j = i + 1; j < m->n_parts; j++)
			ck(m->parts[j].key != m->parts[i].key, "no key twice");
	}
}

static int near(float a, float b)
{
	return a - b < 0.001f && b - a < 0.001f;
}

/* The plan's front-view millimetres (from the front's top left, y down) in
 * the model's (x right, z up, from the middle) */
static int at_front(const td_model *m, td_key k, float fx, float fy, float r)
{
	const td_part *p = td_part_of(m, k);

	return p && p->face == TD_FACE_FRONT && near(p->x, fx - 36.6f) && near(p->z, 54.95f - fy) && near(p->r, r);
}

static void brick(void)
{
	const td_model *m = &td_model_brick;
	const td_part *p;
	int i, on_top = 0, on_ledge = 0;

	keys_once(m);
	ck(m->art == TD_ART_BRICK && m->body_w == 73.2f && m->body_h == 109.9f,
	   "brick: its art, its 73.2 x 109.9 body");
	ck(at_front(m, TD_DPAD, 15.9f, 72.3f, 9.65f), "brick: the d-pad at (15.9, 72.3), 19.3 across");
	ck(at_front(m, TD_A, 63.9f, 72.1f, 3.85f) && at_front(m, TD_B, 56.3f, 79.7f, 3.85f) &&
	   at_front(m, TD_X, 56.3f, 64.5f, 3.85f) && at_front(m, TD_Y, 48.7f, 72.1f, 3.85f),
	   "brick: A B X Y 7.6 from (56.3, 72.1), A right, B bottom, X top, Y left, 7.7 across");
	ck(at_front(m, TD_F1, 30.9f, 63.6f, 3.55f) && at_front(m, TD_F2, 41.6f, 63.6f, 3.55f),
	   "brick: F1 and F2 at y 63.6, 7.1 long");
	ck(at_front(m, TD_MENU, 26.1f, 101.4f, 2.7f) && at_front(m, TD_SELECT, 36.1f, 101.4f, 2.7f) &&
	   at_front(m, TD_START, 46.2f, 101.4f, 2.7f), "brick: MENU, SELECT and START along the bottom, 5.4 across");
	p = td_part_of(m, TD_VOLUP);
	ck(p && p->face == TD_FACE_LEFT && near(p->z, 54.95f - 10.3f) && near(p->r, 3.85f), "brick: VOL+ on the left at y 10.3");
	p = td_part_of(m, TD_VOLDN);
	ck(p && p->face == TD_FACE_LEFT && near(p->z, 54.95f - 21.4f) && near(p->r, 3.85f), "brick: VOL- on the left at y 21.4");
	p = td_part_of(m, TD_POWER);
	ck(p && p->face == TD_FACE_RIGHT && near(p->z, 54.95f - 39.3f) && near(p->r, 3.55f),
	   "brick: POWER on the right at y 39.3, 7.1 across");
	/* The side keys on the thin part, across its middle: 11.8 deep from the
	 * front at -9.95 */
	for (i = 0; i < m->n_parts; i++)
		if (m->parts[i].face == TD_FACE_LEFT || m->parts[i].face == TD_FACE_RIGHT)
			ck(near(m->parts[i].x, -9.95f + 11.8f / 2.0f), "brick: a side key across the thin part's middle");
	/* The shoulders on the back ledge, standing up to 13.65 (y 41.3), left to
	 * right L1, L2, R2, R1; L1 and R1 from the body's edges */
	for (i = 0; i < m->n_parts; i++) {
		on_top += m->parts[i].face == TD_FACE_TOP;
		on_ledge += m->parts[i].face == TD_FACE_LEDGE;
	}
	ck(on_top == 0 && on_ledge == 4, "brick: four keys on the ledge, none on the top edge");
	{
		const td_part *l1 = td_part_of(m, TD_L1), *l2 = td_part_of(m, TD_L2);
		const td_part *r2 = td_part_of(m, TD_R2), *r1 = td_part_of(m, TD_R1);

		ck(l1 && l2 && r2 && r1 && l1->face == TD_FACE_LEDGE && l2->face == TD_FACE_LEDGE &&
		   r2->face == TD_FACE_LEDGE && r1->face == TD_FACE_LEDGE, "brick: L1 L2 R2 R1 on the ledge");
		ck(l1 && r1 && near(l1->z, 13.65f) && near(r1->z, 13.65f) && l2 && r2 && near(l2->z, 13.65f) &&
		   near(r2->z, 13.65f), "brick: the shoulders stand up to 13.65");
		ck(l1 && l2 && r2 && r1 && l1->x < l2->x && l2->x < r2->x && r2->x < r1->x,
		   "brick: L1, L2, R2, R1 from left to right");
		ck(l1 && r1 && near(l1->x - l1->r, -36.6f) && near(r1->x + r1->r, 36.6f) && near(l1->r, 8.75f) &&
		   near(r1->r, 8.75f), "brick: L1 and R1 17.5 wide from the body's edges");
		ck(l2 && r2 && near(l2->x, -12.3f) && near(r2->x, 12.3f) && near(l2->r, 5.5f) && near(r2->r, 5.5f),
		   "brick: L2 and R2 11 wide at -12.3 and 12.3");
	}
	ck(m->lean_centre > 9.6f && m->lean_centre < 12.1f,
	   "brick: SELECT, START and MENU the middle, Y (12.1 right) not");
	ck(view_is(m->front, 10.0f, m->front.s, 0.0f) && view_is(m->right, 52.0f, m->right.s, 0.0f) &&
	   view_is(m->left, -52.0f, m->left.s, 0.0f), "brick: front, right and left as the Pixel 2's turn");
	ck(m->back.deg >= 150.0f && m->back.deg <= 170.0f && m->back.pitch >= 15.0f && m->back.pitch <= 25.0f,
	   "brick: the back view turned round the right side's way, tilted back about 20");
	ck(m->tab.deg == 10.0f && m->tab.pitch == 0.0f, "brick: the Controls tab's front pose turned 10, level");
	/* As tall as the Pixel 2: 109.9 mm at the scale 81 is at 3.55 in the
	 * tutorial, at 3.15 on the Controls tab, give or take 3% */
	ck(m->front.s * 109.9f > 0.97f * 81.0f * 3.55f && m->front.s * 109.9f < 1.03f * 81.0f * 3.55f,
	   "brick: as tall in the tutorial as the Pixel 2");
	ck(m->tab.s * 109.9f > 0.97f * 81.0f * 3.15f && m->tab.s * 109.9f < 1.03f * 81.0f * 3.15f,
	   "brick: as tall on the Controls tab as the Pixel 2");
	ck(m->keep_tour.w > 0 && m->keep_tour.h > 0 && m->keep_tab.w > 0 && m->keep_tab.h > 0,
	   "brick: its kept boxes (--handheld-box holds them)");
}

/* The TrimUI Smart Pro's, as src/device/smartpro_model.c measures them on
 * TrimUI's manual drawing: its 188 x 80 outline, the front's keys on their
 * sides, the top edge's from left to right, its views */
static void smartpro(void)
{
	const td_model *m = &td_model_smartpro;
	const td_part *l1 = td_part_of(m, TD_L1), *l2 = td_part_of(m, TD_L2), *r1 = td_part_of(m, TD_R1);
	const td_part *r2 = td_part_of(m, TD_R2), *pw = td_part_of(m, TD_POWER);
	const td_part *vd = td_part_of(m, TD_VOLDN), *vu = td_part_of(m, TD_VOLUP);
	int i, top = 0, front = 0, sides = 0;

	keys_once(m);
	ck(m->art == TD_ART_SMARTPRO && m->body_w == 188.0f && m->body_h == 80.0f,
	   "smartpro: its art, its 188 x 80 body");
	for (i = 0; i < m->n_parts; i++) {
		const td_part *p = &m->parts[i];

		top += p->face == TD_FACE_TOP;
		front += p->face == TD_FACE_FRONT;
		sides += p->face == TD_FACE_LEFT || p->face == TD_FACE_RIGHT || p->face == TD_FACE_LEDGE;
		ck(fabsf(p->x) + p->r < 94.0f, "smartpro: every key inside the body's width");
		if (p->face == TD_FACE_FRONT) ck(fabsf(p->z) + p->r < 40.0f, "smartpro: a front key inside its height");
	}
	ck(front == 8 && top == 7 && sides == 0, "smartpro: eight keys on the front, seven on the top edge, none on the sides");
	ck(td_part_of(m, TD_DPAD)->x < -60.0f && td_part_of(m, TD_MENU)->x < -60.0f,
	   "smartpro: the d-pad and MENU at the left end");
	ck(td_part_of(m, TD_A)->x > td_part_of(m, TD_Y)->x && td_part_of(m, TD_X)->z > td_part_of(m, TD_B)->z &&
	   td_part_of(m, TD_A)->x > 60.0f, "smartpro: A B X Y at the right end, A right, X top");
	ck(td_part_of(m, TD_START)->x > td_part_of(m, TD_SELECT)->x && td_part_of(m, TD_SELECT)->x > 60.0f,
	   "smartpro: SELECT then START at the right end's foot");
	ck(l1 && l2 && r1 && r2 && pw && vd && vu && l1->x < pw->x && pw->x < vd->x && vd->x < vu->x &&
	   vu->x < r1->x && l2->z > l1->z && r2->z > r1->z,
	   "smartpro: L, POWER, VOL-, VOL+, R along the top, L2 and R2 standing higher");
	ck(!td_part_of(m, TD_F1) && !td_part_of(m, TD_F2), "smartpro: no F1, F2");
	ck(m->top.pitch == TD_PITCH_DEG && m->front.pitch == 0.0f && m->tab.pitch == 0.0f,
	   "smartpro: tilted back for the top edge's keys, level otherwise");
	ck(m->right.deg > 0.0f && m->left.deg < 0.0f, "smartpro: its sides' turns, for how far it may turn");
	ck(m->front.s * 188.0f < 620.0f, "smartpro: as wide in the tutorial as its column on a 16:9 canvas");
	ck(m->keep_tour.w > 0 && m->keep_tour.h > 0 && m->keep_tab.w > 0 && m->keep_tab.h > 0,
	   "smartpro: its kept boxes (--handheld-box holds them)");
}

/* The R36S's as src/device/r36s_model.c measures it on photos of a real
 * one: its keys on their faces, the volume keys on the right and POWER on
 * the left (the Brick's the other way round), the shoulders on the back
 * ledge, and the views at its size */
static void r36s(void)
{
	const td_model *m = &td_model_r36s;
	const td_part *up = td_part_of(m, TD_VOLUP), *dn = td_part_of(m, TD_VOLDN), *pw = td_part_of(m, TD_POWER);
	const td_part *l1 = td_part_of(m, TD_L1), *l2 = td_part_of(m, TD_L2);
	const td_part *r2 = td_part_of(m, TD_R2), *r1 = td_part_of(m, TD_R1);
	int i;

	keys_once(m);
	ck(m->art == TD_ART_R36S && m->body_w == 80.0f && m->body_h == 119.0f, "r36s: its art, its 80 x 119 body");
	ck(is(m, TD_DPAD, TD_FACE_FRONT, -25.0f, -13.5f, 10.75f), "r36s: the d-pad on the left, under the glass");
	ck(is(m, TD_A, TD_FACE_FRONT, 33.6f, -14.1f, 3.6f) && is(m, TD_B, TD_FACE_FRONT, 25.6f, -22.1f, 3.6f) &&
	   is(m, TD_X, TD_FACE_FRONT, 25.6f, -6.1f, 3.6f) && is(m, TD_Y, TD_FACE_FRONT, 17.6f, -14.1f, 3.6f),
	   "r36s: A B X Y on the right, A right, B bottom, X top, Y left");
	ck(is(m, TD_MENU, TD_FACE_FRONT, 0.0f, -22.9f, 3.6f) && is(m, TD_SELECT, TD_FACE_FRONT, -7.4f, -30.4f, 3.6f) &&
	   is(m, TD_START, TD_FACE_FRONT, 7.4f, -30.4f, 3.6f), "r36s: FN in the middle, SELECT and START under it");
	ck(up && dn && up->face == TD_FACE_RIGHT && dn->face == TD_FACE_RIGHT && up->z > dn->z &&
	   near(up->z - up->r, dn->z + dn->r), "r36s: the volume rocker on the right, VOL+ the top half");
	ck(pw && pw->face == TD_FACE_LEFT && pw->z > 30.0f, "r36s: POWER on the left side, near the top");
	ck(!td_part_of(m, TD_F1) && !td_part_of(m, TD_F2), "r36s: no brightness keys of its own");
	for (i = 0; i < m->n_parts; i++)
		if (m->parts[i].face == TD_FACE_LEFT || m->parts[i].face == TD_FACE_RIGHT)
			ck(near(m->parts[i].x, -15.0f + 18.5f / 2.0f), "r36s: a side key across the thin part's middle");
	ck(l1 && l2 && r2 && r1 && l1->face == TD_FACE_LEDGE && l2->face == TD_FACE_LEDGE &&
	   r2->face == TD_FACE_LEDGE && r1->face == TD_FACE_LEDGE, "r36s: L1 L2 R2 R1 on the ledge");
	ck(l1 && l2 && r2 && r1 && l1->x < l2->x && l2->x < 0.0f && r2->x > 0.0f && r2->x < r1->x &&
	   l1->x - l1->r >= -40.0f && r1->x + r1->r <= 40.0f, "r36s: L1, L2, R2, R1 from left to right, inside the body");
	ck(m->lean_centre > 7.4f + 3.6f - 0.01f && m->lean_centre < 17.6f, "r36s: FN, SELECT and START the middle, Y not");
	ck(view_is(m->right, 52.0f, m->right.s, 0.0f) && view_is(m->left, -52.0f, m->left.s, 0.0f) &&
	   m->back.deg >= 150.0f && m->back.deg <= 170.0f, "r36s: the Brick's turns");
	ck(m->front.s * 119.0f > 0.97f * 81.0f * 3.55f && m->front.s * 119.0f < 1.03f * 81.0f * 3.55f,
	   "r36s: as tall in the tutorial as the Pixel 2");
	ck(m->tab.s * 119.0f > 0.97f * 81.0f * 3.15f && m->tab.s * 119.0f < 1.03f * 81.0f * 3.15f,
	   "r36s: as tall on the Controls tab as the Pixel 2");
	ck(m->flat && m->keep_tour.w > 0 && m->keep_tab.w > 0, "r36s: drawn flat, its kept boxes");
	ck(td_spins(m, 1u << TD_DPAD | 1u << TD_L1), "r36s: the d-pad's lesson turns round to L1");
}

/* td_swing: held on the front, turned round, held on the back, turned
 * back, and again; td_spins only with keys on the back ledge and off it */
static void spin(void)
{
	const int h = DS_MS_HOLD_SPIN, s = DS_MS_SPIN;
	float last = 0.0f;
	bool steps = true;
	int ms;

	ck(td_swing(-50) == 0.0f && td_swing(0) == 0.0f && td_swing(h) == 0.0f,
	   "td_swing: on the front, before and through its first hold");
	ck(td_swing(h + s / 2) == 0.5f && td_swing(h + s) == 1.0f && td_swing(2 * h + s) == 1.0f,
	   "td_swing: half way round at half the turn, on the back through its hold");
	ck(td_swing(2 * h + s + s / 2) == 0.5f && td_swing(2 * (h + s)) == 0.0f &&
	   td_swing(2 * (h + s) + h + s) == 1.0f, "td_swing: back again, and the same round after");
	/* No jump anywhere: a few milliseconds never move it more than a turn's step */
	for (ms = 0; ms <= 4 * (h + s); ms += 5) {
		const float k = td_swing(ms);

		if (k < 0.0f || k > 1.0f || fabsf(k - last) > 0.02f) steps = false;
		last = k;
	}
	ck(steps, "td_swing: between 0 and 1, and smooth, every 5 ms of two rounds");
	ck(td_spins(&td_model_brick, 1u << TD_DPAD | 1u << TD_L1) &&
	   td_spins(&td_model_brick, 1u << TD_SELECT | 1u << TD_R2), "td_spins: the Brick's front keys with a shoulder");
	ck(!td_spins(&td_model_brick, 1u << TD_L1 | 1u << TD_R1) && !td_spins(&td_model_brick, 1u << TD_DPAD) &&
	   !td_spins(&td_model_brick, 0), "td_spins: shoulders alone, front keys alone, or nothing");
	ck(!td_spins(&td_model_pixel2, 1u << TD_DPAD | 1u << TD_L1) && !td_spins(NULL, 1u << TD_L1),
	   "td_spins: the Pixel 2's shoulders are on its top, no model is none");
}

int main(void)
{
	const td_model *m = &td_model_pixel2;

	keys_once(m);
	ck(m->art == TD_ART_PIXEL2 && m->body_w == 63.0f && m->body_h == 81.0f && m->lean_centre == 8.0f,
	   "pixel2: its art, its 63 x 81 body, SELECT and START in the middle");
	ck(is(m, TD_DPAD, TD_FACE_FRONT, -16.9f, -21.0f, 9.0f), "pixel2: the d-pad");
	ck(is(m, TD_A, TD_FACE_FRONT, 23.6f, -21.5f, 3.05f) && is(m, TD_B, TD_FACE_FRONT, 16.9f, -28.2f, 3.05f) &&
	   is(m, TD_X, TD_FACE_FRONT, 16.9f, -14.8f, 3.05f) && is(m, TD_Y, TD_FACE_FRONT, 10.2f, -21.5f, 3.05f),
	   "pixel2: A B X Y");
	ck(is(m, TD_SELECT, TD_FACE_FRONT, -3.8f, -29.6f, 2.85f) && is(m, TD_START, TD_FACE_FRONT, 4.0f, -29.6f, 2.85f),
	   "pixel2: SELECT and START");
	ck(is(m, TD_MENU, TD_FACE_RIGHT, -1.54f, 17.13f, 2.65f) && is(m, TD_POWER, TD_FACE_RIGHT, -1.74f, 6.65f, 4.9f),
	   "pixel2: MENU and POWER on the right side");
	ck(is(m, TD_VOLUP, TD_FACE_LEFT, -1.82f, 16.52f, 2.95f) && is(m, TD_VOLDN, TD_FACE_LEFT, -1.82f, 7.05f, 2.95f),
	   "pixel2: the volume keys on the left side");
	ck(is(m, TD_L1, TD_FACE_TOP, -23.9f, 41.7f, 7.55f) && is(m, TD_R1, TD_FACE_TOP, 23.9f, 41.7f, 7.55f) &&
	   is(m, TD_L2, TD_FACE_TOP, -12.6f, 41.7f, 3.7f) && is(m, TD_R2, TD_FACE_TOP, 12.6f, 41.7f, 3.7f),
	   "pixel2: the shoulders on the top edge, L1/R1 outside");
	ck(!td_part_of(m, TD_F1) && !td_part_of(m, TD_F2) && !td_part_of(m, TD_KEYS), "pixel2: no F1, F2");
	ck(view_is(m->front, 10.0f, 3.55f, 0.0f) && view_is(m->right, 52.0f, 3.95f, 0.0f) &&
	   view_is(m->left, -52.0f, 3.95f, 0.0f) && view_is(m->both, 0.0f, 3.45f, 0.0f) &&
	   view_is(m->top, 10.0f, 3.55f, TD_PITCH_DEG), "pixel2: the tutorial's five views");
	ck(m->keep_tour.l == 0 && m->keep_tour.t == 66 && m->keep_tour.w == 304 && m->keep_tour.h == 382,
	   "pixel2: the tutorial's kept box");
	ck(m->keep_tab.l == 128 && m->keep_tab.t == 176 && m->keep_tab.w == 256 && m->keep_tab.h == 332,
	   "pixel2: the Controls tab's kept box");
	ck(view_is(m->tab, 10.0f, 3.15f, 0.0f), "pixel2: the Controls tab's pose, 3.15 turned 10");
	{
		int i, ledge = 0;

		for (i = 0; i < m->n_parts; i++) ledge += m->parts[i].face == TD_FACE_LEDGE;
		ck(!ledge && view_is(m->back, m->front.deg, m->front.s, m->front.pitch),
		   "pixel2: no key on a ledge, its back view the front");
	}
	brick();
	smartpro();
	r36s();
	spin();
	if (fails) {
		printf("model: %d of %d checks failed\n", fails, checks);
		return 1;
	}
	printf("model: %d checks passed\n", checks);
	return 0;
}
