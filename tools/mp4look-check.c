/* SPDX-License-Identifier: GPL-3.0-only */
/* MP4's Player Looks' motion (src/mp4look.h) held to its numbers: a bar up
 * at once and down on follow's tau, the same at any frame rate; its cap held
 * for peak-hold and then down with gravity in MP4L_CAP_DROP_MS; every bar at
 * rest soon after the sound stops; the bands shared out over a look's bars;
 * the setting's words both ways.
 *
 *   make check-mp4look
 */
#include "../src/design.h"
#include "../src/mp4look.h"
#include "../src/device/profile.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* The tape's speed in the drawing's units, as src/cassette.h has it */
#define CAS_SPEED (47.6f * 592 / 100.4f)

static int fails;

static void fail(const char *what, double got, double want)
{
	printf("FAIL: %s: %.4f, not %.4f\n", what, got, want);
	fails++;
}

static void near(const char *what, double got, double want, double tol)
{
	if (fabs(got - want) > tol) fail(what, got, want);
}

/* `ms` exactly, in frames `dt` apart and the rest in one, with `bands` or
 * none */
static void run(mp4l_bars_t *b, float ms, float dt, const uint8_t *bands)
{
	int n = (int)(ms / dt), i;

	for (i = 0; i < n; i++) mp4l_bars_step(b, dt, bands);
	if (ms - (float)n * dt > 0.0f) mp4l_bars_step(b, ms - (float)n * dt, bands);
}

int main(void)
{
	static uint8_t full[MP4L_BANDS], one[MP4L_BANDS];
	mp4l_bars_t b;
	int i, look;

	memset(full, 255, sizeof full);

	/* The words: each look's, round; one nobody knows is Cover */
	for (look = 0; look < MP4L_COUNT; look++)
		if ((int)mp4l_from_key(mp4l_key((mp4l_id)look)) != look) fail("a look's word back", look, look);
	if (mp4l_from_key("tape-deck") != MP4L_COVER || mp4l_from_key(NULL) != MP4L_COVER)
		fail("an unknown word is Cover", 1, 0);
	if (mp4l_bars(MP4L_COVER) != 0) fail("Cover has bars", mp4l_bars(MP4L_COVER), 0);
	if (mp4l_bars(MP4L_SPECTRUM) != 32) fail("Spectrum's bars", mp4l_bars(MP4L_SPECTRUM), 32);

	/* Up at once */
	mp4l_bars_reset(&b, 32);
	mp4l_bars_step(&b, 16.7f, full);
	near("a bar after one frame of full sound", b.lv[0], 1.0, 1e-6);
	near("its cap with it", b.cap[0], 1.0, 1e-6);

	/* Down on follow: after one tau, e^-1 of the way left, at 60 and at 30
	 * frames a second alike */
	run(&b, (float)DS_MS_BAR_FALL, 1000.0f / 60, NULL);
	near("a bar one tau after the sound stops, at 60", b.lv[0], exp(-1.0), 0.002);
	mp4l_bars_reset(&b, 32);
	mp4l_bars_step(&b, 0, full);
	run(&b, (float)DS_MS_BAR_FALL, 1000.0f / 30, NULL);
	near("...and at 30", b.lv[0], exp(-1.0), 0.002);

	/* The cap: held for peak-hold, then down with gravity: at the floor
	 * MP4L_CAP_DROP_MS after (the bar under it long gone) */
	mp4l_bars_reset(&b, 32);
	mp4l_bars_step(&b, 0, full);
	run(&b, (float)DS_MS_PEAK_HOLD - 10, 10, NULL);
	near("the cap just before peak-hold ends", b.cap[0], 1.0, 1e-6);
	run(&b, 210, 10, NULL);
	near("200 ms into its fall", b.cap[0], 1.0 - 0.5 * (2.0 / (MP4L_CAP_DROP_MS * (double)MP4L_CAP_DROP_MS)) * 200 * 200, 0.002);
	mp4l_bars_reset(&b, 32);
	mp4l_bars_step(&b, 0, full);
	run(&b, (float)DS_MS_PEAK_HOLD + MP4L_CAP_DROP_MS - 30, 1000.0f / 60, NULL);
	if (b.cap[0] <= 0.0f) fail("the cap on the floor early", b.cap[0], 0.01);
	run(&b, 60, 1000.0f / 60, NULL);
	near("the cap on the floor after its hold and its drop", b.cap[0], 0.0, 1e-6);

	/* A bar coming up under a falling cap catches it: the cap rides on it */
	mp4l_bars_reset(&b, 32);
	mp4l_bars_step(&b, 0, full);
	run(&b, 600, 10, NULL);
	memset(one, 230, sizeof one);
	mp4l_bars_step(&b, 10, one);
	near("a cap a bar has come up to", b.cap[0], 230.0 / 255, 1e-6);

	/* At rest: a second after the sound stops nothing moves */
	mp4l_bars_reset(&b, 32);
	mp4l_bars_step(&b, 0, full);
	if (mp4l_bars_rest(&b)) fail("at rest with full sound", 1, 0);
	run(&b, (float)DS_MS_PEAK_HOLD + MP4L_CAP_DROP_MS + 400, 1000.0f / 60, NULL);
	if (!mp4l_bars_rest(&b)) fail("not at rest after the cap's hold and drop", b.lv[0] + b.cap[0], 0);

	/* The bands over 18 bars: band 31 alone lights the last bar alone, and
	 * band 0 the first */
	mp4l_bars_reset(&b, 18);
	memset(one, 0, sizeof one);
	one[31] = 255;
	mp4l_bars_step(&b, 0, one);
	for (i = 0; i < 17; i++)
		if (b.lv[i] != 0.0f) fail("band 31 lights a bar before the last", i, 17);
	near("band 31 on the last bar", b.lv[17], 1.0, 1e-6);
	mp4l_bars_reset(&b, 18);
	memset(one, 0, sizeof one);
	one[0] = 128;
	mp4l_bars_step(&b, 0, one);
	near("band 0 on the first bar", b.lv[0], 128.0 / 255, 1e-6);
	for (i = 1; i < 18; i++)
		if (b.lv[i] != 0.0f) fail("band 0 lights a bar after the first", i, 0);

	/* Turning: up over spin-up on out, down over spin-down, and from
	 * wherever it is when the other comes */
	{
		mp4l_spin sp = { 0 };
		float v;

		mp4l_spin_to(&sp, true);
		mp4l_spin_step(&sp, (float)DS_MS_SPIN_UP / 2);
		near("half way up", sp.v, ds_out(0.5f), 1e-5);
		mp4l_spin_step(&sp, (float)DS_MS_SPIN_UP / 2);
		near("up after spin-up", sp.v, 1.0, 1e-6);
		mp4l_spin_to(&sp, false);
		mp4l_spin_step(&sp, (float)DS_MS_SPIN_DOWN);
		near("down after spin-down", sp.v, 0.0, 1e-6);
		if (mp4l_spin_moving(&sp)) fail("still turning after spin-down", sp.v, 0);
		mp4l_spin_to(&sp, true);
		mp4l_spin_step(&sp, 100);
		v = sp.v;
		mp4l_spin_to(&sp, false);
		mp4l_spin_step(&sp, 0);
		near("a pause in the middle of spin-up starts down from there", sp.v, v, 1e-6);
		mp4l_spin_step(&sp, (float)DS_MS_SPIN_DOWN);
		near("...and is down a spin-down later", sp.v, 0.0, 1e-6);
	}

	/* The tape: the packs trade their area over the song; each reel turns
	 * at the tape's speed over its own radius; a new song rewinds it over
	 * rewind on turn; a seek winds on follow */
	{
		const float hub = 63.4f, full = 148.0f, speed = 280.0f, wind = 900.0f;
		mp4l_tape tp = { 0 };
		float a0, a1, rl, rr;

		near("the left pack full at the start", mp4l_tape_r(0, 0, hub, full), full, 1e-3);
		near("the right one empty", mp4l_tape_r(0, 1, hub, full), hub, 1e-3);
		near("halfway, the two equal", mp4l_tape_r(0.5f, 0, hub, full), mp4l_tape_r(0.5f, 1, hub, full), 1e-3);
		near("...each half the tape's area", mp4l_tape_r(0.5f, 0, hub, full),
		     sqrt((hub * hub + full * full) / 2), 1e-2);

		mp4l_tape_step(&tp, 0, 3, 0.25f, 1.0f, speed, hub, full, wind);
		near("the tape where the song is when first seen", tp.f, 0.25, 1e-6);
		a0 = tp.ang[0];
		a1 = tp.ang[1];
		mp4l_tape_step(&tp, 1000, 3, 0.25f, 1.0f, speed, hub, full, wind);
		rl = mp4l_tape_r(0.25f, 0, hub, full);
		rr = mp4l_tape_r(0.25f, 1, hub, full);
		near("the left reel's turn in a second, counterclockwise", fmodf(a0 - tp.ang[0] + 720, 360),
		     fmodf(speed / rl * 57.29578f, 360), 0.05);
		near("the right reel's, faster for its smaller pack", fmodf(a1 - tp.ang[1] + 720, 360),
		     fmodf(speed / rr * 57.29578f, 360), 0.05);

		/* A new song at 0.6: halfway through rewind, the turn's half */
		tp = (mp4l_tape){ 0 };
		mp4l_tape_step(&tp, 0, 1, 0.6f, 0.0f, speed, hub, full, wind);
		a0 = tp.ang[0];
		mp4l_tape_step(&tp, (float)DS_MS_REWIND / 2, 2, 0.0f, 0.0f, speed, hub, full, wind);
		near("half way through a rewind", tp.f, 0.6 * (1 - ds_turn(0.5f)), 1e-4);
		if (fmodf(tp.ang[0] - a0 + 720, 360) <= 0 || fmodf(tp.ang[0] - a0 + 720, 360) > 180)
			fail("a rewind turns the reels clockwise", fmodf(tp.ang[0] - a0 + 720, 360), 90);
		if (!mp4l_tape_moving(&tp, 0)) fail("not moving in a rewind", tp.f, 0.3);
		mp4l_tape_step(&tp, (float)DS_MS_REWIND / 2 + 1, 2, 0.0f, 0.0f, speed, hub, full, wind);
		near("at the start after rewind", tp.f, 0.0, 1e-6);
		if (mp4l_tape_moving(&tp, 0)) fail("moving after its rewind", tp.f, 0);

		/* A seek from 0.2 to 0.3: a tau later e^-1 of the way left */
		tp = (mp4l_tape){ 0 };
		mp4l_tape_step(&tp, 0, 1, 0.2f, 1.0f, speed, hub, full, wind);
		for (int k = 0; k < 18; k++) mp4l_tape_step(&tp, (float)DS_MS_WIND / 18, 1, 0.3f, 1.0f, speed, hub, full, wind);
		near("a seek wound a tau's way", 0.3 - tp.f, 0.1 * exp(-1.0), 0.002);
	}

	/* The record: 33 1/3 a minute is 200 degrees a second, clockwise; the
	 * needle glides on follow; and the arm's angle puts the needle on the
	 * radius asked, on the record's right, at every song's ring */
	{
		mp4l_disc dk = { 0 };
		const float d = 302.0f, pa = -40.0f, len = 323.0f, out = 205.0f, in = 84.0f;
		int place;

		mp4l_disc_step(&dk, 0, 1.0f, 150.0f);
		mp4l_disc_step(&dk, 1000, 1.0f, 150.0f);
		near("the record's turn in a second", dk.ang, 200.0, 1e-3);
		dk.known = true;
		dk.arm = 150.0f;
		for (int k = 0; k < 18; k++) mp4l_disc_step(&dk, (float)DS_MS_ARM_GLIDE / 18, 0, 120.0f);
		near("the needle a tau's way to the next song", dk.arm - 120.0f, 30.0 * exp(-1.0), 0.05);
		near("the first song's start at the grooves' edge", mp4l_groove_r(0, 8, 0, out, in), out, 1e-4);
		near("the last song's end at their inner edge", mp4l_groove_r(7, 8, 1, out, in), in, 1e-4);
		near("song 3 of 8 halfway", mp4l_groove_r(2, 8, 0.5f, out, in), out - (out - in) * 2.5 / 8, 1e-3);
		for (place = 0; place <= 8; place++) {
			float r = mp4l_groove_r(place, 8, 0, out, in), a = mp4l_arm_angle(r, d, pa, len) * (float)M_PI / 180;
			float px = d * cosf(pa * (float)M_PI / 180), py = d * sinf(pa * (float)M_PI / 180);
			float nx = px + len * cosf(a), ny = py + len * sinf(a), at = atan2f(ny, nx) * 180 / (float)M_PI;

			near("the needle on its song's ring", hypotf(nx, ny), r, 0.01);
			if (at < 20 || at > 75) fail("the needle off the record's lower right (degrees)", at, 45);
		}
	}

	/* The cassette's styles: every word back, Smoke the default and for a
	 * word nobody knows, Up and Down round the kept ones only */
	{
		int k, seen[MP4T_COUNT] = { 0 }, n = 0;
		mp4t_id t = MP4T_SMOKE;

		for (k = 0; k < MP4T_COUNT; k++)
			if ((int)mp4t_from_key(mp4t_key((mp4t_id)k)) != k) fail("a style's word back", k, k);
		if (mp4t_from_key(NULL) != MP4T_SMOKE || mp4t_from_key("chrome") != MP4T_SMOKE)
			fail("an unknown style is Smoke", 1, 0);
		if (strcmp(mp4t_key(MP4T_SMOKE), "smoke") || strcmp(mp4t_key(MP4T_BAND), "band"))
			fail("the words as kept", 1, 0);
		for (k = 0; k < MP4T_COUNT; k++) if (!mp4t_kept((mp4t_id)k)) fail("all eight kept", k, -1);
		for (k = 0; k < MP4T_COUNT; k++) {
			t = mp4t_step(t, +1);
			if (!mp4t_kept(t)) fail("Down onto a style not kept", t, -1);
			if (!seen[t]++) n++;
		}
		if (t != MP4T_SMOKE || n != MP4T_COUNT) fail("Down round the eight and back to Smoke", n, MP4T_COUNT);
		if (mp4t_step(MP4T_SMOKE, -1) != MP4T_WORN || mp4t_step(MP4T_SMOKE, +1) != MP4T_PINSTRIPE)
			fail("Up from Smoke is Worn, Down Pinstripe", mp4t_step(MP4T_SMOKE, -1), MP4T_WORN);
		for (k = 0; k < MP4T_COUNT; k++)
			if (mp4t_step(mp4t_step((mp4t_id)k, +1), -1) != (mp4t_id)k) fail("Down then Up back", k, k);
		if (mp4t_pixel(MP4T_SMOKE)) fail("Smoke drawn smooth", 1, 0);
		for (k = 1; k < MP4T_COUNT; k++) if (!mp4t_pixel((mp4t_id)k)) fail("the others pixel art", k, 1);
	}

	/* The tape's number and the side, as the lab has them: a 38-minute
	 * album a 46, 52 a 60, 71 a 90, past 90 a 120; the first half A */
	{
		static const struct { double min; int no; } NO[] = {
			{ 38, 46 }, { 46, 46 }, { 46.1, 60 }, { 52, 60 }, { 60, 60 }, { 71, 90 }, { 90, 90 },
			{ 91, 120 }, { 200, 120 }, { 1, 46 },
		};
		unsigned k;

		for (k = 0; k < sizeof NO / sizeof NO[0]; k++)
			if (mp4t_tape_no(NO[k].min * 60) != NO[k].no) fail("the tape's number (minutes)", NO[k].min, NO[k].no);
		if (mp4t_tape_no(0) != 0) fail("no number for a length not known", mp4t_tape_no(0), 0);
		if (mp4t_side(0, 8) != 'A' || mp4t_side(3, 8) != 'A' || mp4t_side(4, 8) != 'B' || mp4t_side(7, 8) != 'B')
			fail("eight songs: four a side", mp4t_side(4, 8), 'B');
		if (mp4t_side(4, 9) != 'A' || mp4t_side(5, 9) != 'B') fail("nine: the odd one on A", mp4t_side(4, 9), 'A');
		if (mp4t_side(0, 1) != 'A' || mp4t_side(0, 0) != 'A') fail("one song: side A", 1, 'A');
	}

	/* The sprites the frame picks follow the model: the hubs' frame is the
	 * angle's nearest 7.5 degrees, all 48 met in a turn, stepping down as
	 * they turn counterclockwise playing; a rewind's fastest step under 30
	 * degrees a frame (the teeth's half period), so the frame never seems
	 * to run back; each pack the nearest half cell of its radius */
	{
		mp4l_tape tp;
		mp4l_spin sp = { .v = 1, .to = 1 };
		const float cell = 8.0f / (960.0f / 592.0f);   /* units of the drawing a cell, 8-unit cells */
		int k, last, hit[MP4T_HUB_FRAMES] = { 0 }, met = 0, back = 0;
		float worst = 0;

		for (k = 0; k < 480; k++) {
			float a = (float)k * 0.75f;

			if (!hit[mp4t_hub_frame(-a)]++) met++;
			if (fabsf(-a - (float)mp4t_hub_frame(-a) * MP4T_HUB_STEP) > MP4T_HUB_STEP / 2 &&
			    fabsf(-a + 360.0f - (float)mp4t_hub_frame(-a) * MP4T_HUB_STEP) > MP4T_HUB_STEP / 2)
				fail("a hub frame off its angle (degrees)", -a, mp4t_hub_frame(-a) * MP4T_HUB_STEP);
		}
		if (met != MP4T_HUB_FRAMES) fail("the hub frames a turn shows", met, MP4T_HUB_FRAMES);
		if (mp4t_hub_frame(359.0f) != 0 || mp4t_hub_frame(-3.0f) != 0 || mp4t_hub_frame(-4.0f) != 47 ||
		    mp4t_hub_frame(7.5f) != 1)
			fail("the hub frame round the turn's ends", mp4t_hub_frame(-4.0f), 47);
		memset(&tp, 0, sizeof tp);
		mp4l_tape_step(&tp, 0, 0, 0.0f, 1, CAS_SPEED, 63.4f, 148.0f, 800);
		last = mp4t_hub_frame(tp.ang[0]);
		for (k = 0; k < 60; k++) {
			int f;

			mp4l_spin_step(&sp, 1000.0f / 60);
			mp4l_tape_step(&tp, 1000.0f / 60, 0, (float)k / 60 / 214, sp.v, CAS_SPEED, 63.4f, 148.0f, 800);
			f = mp4t_hub_frame(tp.ang[1]);
			if (((f - last + MP4T_HUB_FRAMES) % MP4T_HUB_FRAMES) > MP4T_HUB_FRAMES / 2 || f == last) {
				/* down (counterclockwise) or still between two frames */
			} else back++;
			last = f;
		}
		if (back) fail("the right hub's frame stepping clockwise while playing", back, 0);
		/* A rewind from the song's end: the empty hub's fastest step */
		memset(&tp, 0, sizeof tp);
		mp4l_tape_step(&tp, 0, 0, 1.0f, 0, CAS_SPEED, 63.4f, 148.0f, 800);
		for (k = 0; k < 60; k++) {
			float was = tp.ang[0], d;

			mp4l_tape_step(&tp, 1000.0f / 60, 1, 0.0f, 0, CAS_SPEED, 63.4f, 148.0f, 800);
			d = fabsf(fmodf(tp.ang[0] - was + 540.0f, 360.0f) - 180.0f);
			if (d > worst) worst = d;
		}
		if (worst >= 30.0f || worst < MP4T_HUB_STEP) fail("a rewind's fastest hub step (degrees a frame)", worst, 26);
		if (mp4t_pack_half(63.4f / cell) != 26 || mp4t_pack_half(148.0f / cell) != 60)
			fail("the packs' half cells, empty and full", mp4t_pack_half(148.0f / cell), 60);
		if (mp4t_pack_half(mp4l_tape_r(0.5f, 0, 63.4f, 148.0f) / cell) != 46)
			fail("a pack halfway", mp4t_pack_half(mp4l_tape_r(0.5f, 0, 63.4f, 148.0f) / cell), 46);
	}

	/* The cell is whole pixels on every handheld: 8 units on the Pixel 2
	 * (5) and the Brick (8), 16 on the Smart Pro (15) */
	{
		static const struct { const device_profile *p; int cell, px; } DEV[] = {
			{ &device_profile_pixel2, 8, 5 }, { &device_profile_brick, 8, 8 }, { &device_profile_smartpro, 16, 15 },
		};
		unsigned k;

		for (k = 0; k < sizeof DEV / sizeof DEV[0]; k++) {
			const device_profile *p = DEV[k].p;
			float ppu = mp4t_px_per_unit(p->screen.panel_w, p->screen.panel_h, p->screen.turn);
			int c = mp4t_cell(ppu);

			if (c != DEV[k].cell) fail(p->id, c, DEV[k].cell);
			if (fabsf((float)c * ppu - (float)DEV[k].px) > 1e-3f) fail("a cell's pixels", c * ppu, DEV[k].px);
		}
		if (mp4t_cell(0.7f) != 0) fail("no cell where neither is whole", mp4t_cell(0.7f), 0);
	}

	if (fails) {
		printf("mp4look-check: %d failed\n", fails);
		return 1;
	}
	printf("mp4look-check: bars up at once and down on follow at any rate, caps held %d ms and down in %d,"
	       " at rest after, bands over the bars, the setting's words; reels up in %d ms and down in %d from"
	       " wherever they are, each at the tape's speed over its pack, a rewind in %d ms, a seek wound; the record at"
	       " 33 1/3, the needle gliding and on every song's ring;"
	       " the tape's eight styles, Smoke the default and Up and Down round the kept ones, its number and"
	       " side, the sprite frames from the model, a cell whole pixels on each handheld\n",
	       DS_MS_PEAK_HOLD, MP4L_CAP_DROP_MS, DS_MS_SPIN_UP, DS_MS_SPIN_DOWN, DS_MS_REWIND);
	return 0;
}
