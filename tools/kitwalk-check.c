/* SPDX-License-Identifier: GPL-3.0-only */
/* make check-kitwalk: the Brick kit's lights walk (src/device/brick_kit_walk.h),
 * on a clock moved by hand. The LEDs found among the kernel's names in its
 * order by number; the walk: the 5 V on before any write and off after the
 * last, what the kernel held lit cleared first, each LED alone at the kit's
 * level for its second, one write at a time 40 ms apart, never two steps
 * in one pass; a stop from anywhere turns off what is lit and the 5 V, and
 * lights nothing more; the row's words as it goes. */
#include <stdio.h>
#include <string.h>

#include "../src/device/brick_kit_walk.h"

static int fails, checks;

static void ck(int cond, const char *what)
{
	checks++;
	if (!cond) {
		fails++;
		printf("  FAIL %s\n", what);
	}
}

/* The kernel's 23 LEDs as readdir gives them: in no order */
static char names[KW_NAMES][KW_NAME];
static int  nnames;

static void make_names(int n, bool one_channel)
{
	static const char rgb[] = "rgb";
	int i, c, k = 0;

	nnames = 0;
	/* A fixed shuffle: LED (i * 7) % n first, its blue before its red */
	for (i = 0; i < n; i++)
		for (c = 2; c >= 0; c--) {
			int led = (i * 7) % n;

			if (one_channel && c) continue;
			if (one_channel) snprintf(names[k++], KW_NAME, "led%d", led);
			else snprintf(names[k++], KW_NAME, "sunxi_led%d%c", led, rgb[c]);
		}
	nnames = k;
}

/* A walk run on the hand clock, every step recorded */
typedef struct {
	kw_act   act[512];
	unsigned at[512];
	int      n;
	bool     two_in_a_pass;   /* kw_next gave a second step at the same moment */
} trace;

static unsigned clock_ms;

/* Steps until the walk ends, `stop_at` (when not KW_NEVER) the moment of a
 * stop; the clock moved to each step's due time, as the page's wake does */
static void run(kw_walk *w, trace *t, unsigned stop_at, unsigned stop_late)
{
	kw_act a;
	bool stopped = false;

	t->n = 0;
	t->two_in_a_pass = false;
	while (kw_running(w) && t->n < 512) {
		unsigned due = kw_due(w);

		if (!stopped && stop_at != KW_NEVER && (int)(due - stop_at) > 0) {
			clock_ms = stop_at;
			kw_stop(w, clock_ms);
			stopped = true;
			continue;
		}
		if ((int)(due - clock_ms) > 0) clock_ms = due;
		/* A page held up past the due time (the motor's 300 ms) */
		clock_ms += stop_late;
		if (!kw_next(w, clock_ms, &a)) {
			if (kw_running(w)) { printf("  (no step at its due time)\n"); break; }
			continue;
		}
		t->act[t->n] = a;
		t->at[t->n++] = clock_ms;
		if (kw_next(w, clock_ms, &a)) {
			t->two_in_a_pass = true;
			t->act[t->n] = a;
			t->at[t->n++] = clock_ms;
		}
	}
}

/* What the trace did, checked against the rules every walk keeps */
static void rules(const trace *t, const kw_led *leds, int n, const char *what, bool stopped)
{
	unsigned char lvl[KW_LEDS][3];
	bool power = false, gap_ok = true, set_dark = false, one_at_a_time = true, over = false;
	int i, k, c, sets = 0;
	char msg[160];

	memset(lvl, 0, sizeof lvl);
	for (i = 0; i < t->n; i++) {
		const kw_act *a = &t->act[i];

		if (i && t->at[i] - t->at[i - 1] < KW_GAP_MS) gap_ok = false;
		if (a->kind == KW_ACT_POWER_ON) power = true;
		else if (a->kind == KW_ACT_POWER_OFF) power = false;
		else {
			sets++;
			if (!power) set_dark = true;
			if (a->value > KW_LEVEL) over = true;
			lvl[a->led][a->ch] = (unsigned char)a->value;
		}
		/* After each step: lit channels on one LED at most */
		for (k = 0, c = 0; k < n; k++) {
			int j;

			for (j = 0; j < leds[k].nch; j++)
				if (lvl[k][j]) { c++; break; }
		}
		if (c > 1) one_at_a_time = false;
	}
	snprintf(msg, sizeof msg, "%s: the first step is the 5 V on, the last the 5 V off", what);
	ck(t->n >= 2 && t->act[0].kind == KW_ACT_POWER_ON && t->act[t->n - 1].kind == KW_ACT_POWER_OFF, msg);
	snprintf(msg, sizeof msg, "%s: no write with the 5 V off", what);
	ck(!set_dark, msg);
	snprintf(msg, sizeof msg, "%s: two steps never under %d ms apart", what, KW_GAP_MS);
	ck(gap_ok, msg);
	snprintf(msg, sizeof msg, "%s: never two steps in one pass", what);
	ck(!t->two_in_a_pass, msg);
	snprintf(msg, sizeof msg, "%s: one LED lit at a time, at most", what);
	ck(one_at_a_time, msg);
	snprintf(msg, sizeof msg, "%s: no channel past the kit's level (%d)", what, KW_LEVEL);
	ck(!over, msg);
	for (k = 0, c = 0; k < n; k++)
		for (i = 0; i < leds[k].nch; i++) c += lvl[k][i] != 0;
	snprintf(msg, sizeof msg, "%s: every channel at 0 at the end", what);
	ck(c == 0, msg);
	(void)stopped;
	(void)sets;
}

int main(void)
{
	static kw_led leds[KW_LEDS];
	static unsigned char held[KW_LEDS][3];
	kw_walk w;
	trace t;
	kw_act a;
	char text[64];
	int n, i, k, order_ok = 1, ch_ok = 1;

	/* ---- the LEDs among the names ---- */
	make_names(23, false);
	n = kw_group((const char (*)[KW_NAME])names, nnames, leds, KW_LEDS);
	ck(n == 23, "69 names: 23 LEDs");
	for (i = 0; i < n; i++) {
		char want[KW_NAME];

		if (leds[i].num != i) order_ok = 0;
		snprintf(want, sizeof want, "sunxi_led%d", i);
		if (strcmp(leds[i].stem, want) || leds[i].nch != 3) ch_ok = 0;
		for (k = 0; k < leds[i].nch && ch_ok; k++) {
			char cn[KW_NAME];

			snprintf(cn, sizeof cn, "sunxi_led%d%c", i, "rgb"[k]);
			if (strcmp(names[leds[i].ch[k]], cn)) ch_ok = 0;
		}
	}
	ck(order_ok, "in the kernel's order by number: 0, 1, 2 ... 22, not 0, 1, 10, 11");
	ck(ch_ok, "each LED's red, green and blue, in that order, whatever order the names came in");

	{
		static const char mixed[][KW_NAME] = { "usb", "sunxi_led1g", "power", "sunxi_led1r", "sunxi_led0b", "kbd_backlight" };
		kw_led m[8];
		int nm = kw_group(mixed, 6, m, 8);

		ck(nm == 5, "names without a channel's letter after a digit are LEDs of their own (usb, power, kbd_backlight)");
		ck(nm == 5 && !strcmp(m[0].stem, "kbd_backlight") && !strcmp(m[1].stem, "power") &&
		   !strcmp(m[2].stem, "sunxi_led0") && !strcmp(m[3].stem, "sunxi_led1") && !strcmp(m[4].stem, "usb"),
		   "mixed names: by name, then by number");
		ck(nm == 5 && m[3].nch == 2 && !strcmp(mixed[m[3].ch[0]], "sunxi_led1r") && !strcmp(mixed[m[3].ch[1]], "sunxi_led1g"),
		   "an LED with two of its channels: those two, red first");
		ck(nm == 5 && m[0].nch == 1 && m[0].num == -1 && m[2].nch == 1, "one channel each for a plain name and a lone blue");
		ck(kw_group(mixed, 6, m, 2) == 2, "no more LEDs than room for");
	}

	/* ---- a whole walk ---- */
	clock_ms = 1000;
	memset(&w, 0, sizeof w);
	kw_text(&w, text, sizeof text);
	ck(!strcmp(text, "Press A (one at a time)"), "before A: Press A (one at a time)");
	ck(kw_due(&w) == KW_NEVER && !kw_running(&w), "before A: nothing due");
	kw_start(&w, leds, n, NULL, clock_ms);
	kw_text(&w, text, sizeof text);
	ck(!strcmp(text, "Light 1 of 23"), "at A: Light 1 of 23");
	run(&w, &t, KW_NEVER, 0);
	rules(&t, leds, n, "a whole walk", false);
	ck(w.state == KW_DONE && t.n == 2 + 23 * 6, "a whole walk: the 5 V twice and six writes an LED, 140 steps");
	{
		int lit = 0, ok = 1, hold_ok = 1;

		for (i = 0; i < t.n; i++) {
			if (t.act[i].kind != KW_ACT_SET || !t.act[i].value || t.act[i].ch != 0) continue;
			if (t.act[i].led != lit) ok = 0;
			/* its three on, then held, then its first off */
			if (i + 3 >= t.n || t.at[i + 3] - t.at[i + 2] < KW_LIT_MS || t.act[i + 3].value) hold_ok = 0;
			lit++;
		}
		ck(ok && lit == 23, "a whole walk: each LED lit once, in order");
		ck(hold_ok, "a whole walk: each LED white, all three channels, for its second before the first goes off");
		ck(t.at[1] - t.at[0] >= KW_POWER_MS, "a whole walk: the first write 50 ms after the 5 V");
		ck(t.at[t.n - 1] - t.at[0] > 23u * KW_LIT_MS && t.at[t.n - 1] - t.at[0] < 30000,
		   "a whole walk: between 23 and 30 seconds");
	}
	kw_text(&w, text, sizeof text);
	ck(!strcmp(text, "23 lit one at a time"), "after it: 23 lit one at a time");
	ck(kw_due(&w) == KW_NEVER && !kw_next(&w, clock_ms + 100000, &a), "after it: nothing more");

	/* The row as it goes: LED 5 lit, "Light 5 of 23" */
	clock_ms = 5000;
	kw_start(&w, leds, n, NULL, clock_ms);
	for (i = 0; i < 1 + 4 * 6 + 3; i++) {
		clock_ms = kw_due(&w);
		kw_next(&w, clock_ms, &a);
	}
	kw_text(&w, text, sizeof text);
	ck(!strcmp(text, "Light 5 of 23"), "as it goes: Light 5 of 23 while the fifth is lit");

	/* ---- what the kernel held lit, cleared first ---- */
	memset(held, 0, sizeof held);
	held[5][0] = 255;
	held[22][2] = 60;
	clock_ms = 0xffffff00u;   /* and over the clock's wrap */
	kw_start(&w, leds, n, (const unsigned char (*)[3])held, clock_ms);
	run(&w, &t, KW_NEVER, 0);
	rules(&t, leds, n, "held lit at the start", false);
	ck(t.n >= 3 && t.act[1].kind == KW_ACT_SET && t.act[1].led == 5 && t.act[1].ch == 0 && t.act[1].value == 0 &&
	   t.act[2].kind == KW_ACT_SET && t.act[2].led == 22 && t.act[2].ch == 2 && t.act[2].value == 0,
	   "held lit at the start: LED 6's red and LED 23's blue to 0 first, before anything is lit");
	ck(w.state == KW_DONE && t.n == 2 + 2 + 23 * 6, "held lit at the start: then the whole walk, over the clock's wrap");

	/* ---- stops ---- */
	{
		/* In LED 8's second: its three off, then the 5 V */
		unsigned stop;
		int j, after = 0, lit_after = 0;

		clock_ms = 1000;
		kw_start(&w, leds, n, NULL, clock_ms);
		/* the 5 V, then 7 LEDs of 6 writes, then LED 8's three on */
		for (i = 0; i < 1 + 7 * 6 + 3; i++) {
			clock_ms = kw_due(&w);
			kw_next(&w, clock_ms, &a);
		}
		stop = clock_ms + 500;
		run(&w, &t, stop, 0);
		/* the trace is only what came after the stop; check it with the 5 V
		 * on before it, as it was */
		ck(w.state == KW_STOPPED, "stopped in LED 8's second: stopped");
		for (j = 0; j < t.n; j++) {
			after++;
			if (t.act[j].kind == KW_ACT_SET && t.act[j].value) lit_after++;
		}
		ck(after == 4 && t.act[0].kind == KW_ACT_SET && t.act[0].led == 7 && t.act[0].value == 0 &&
		   t.act[1].led == 7 && t.act[2].led == 7 && t.act[3].kind == KW_ACT_POWER_OFF,
		   "stopped in LED 8's second: its three channels to 0, then the 5 V off, and nothing else");
		ck(lit_after == 0, "stopped: nothing lit after the stop");
		ck(t.n && t.at[0] == stop, "stopped in its second: the first off at once");
		for (j = 1; j < t.n && t.at[j] - t.at[j - 1] >= KW_GAP_MS; j++) {}
		ck(j == t.n, "stopped: its steps 40 ms apart too");
		kw_text(&w, text, sizeof text);
		ck(!strcmp(text, "Stopped at 8 of 23"), "stopped: Stopped at 8 of 23");
		ck(kw_due(&w) == KW_NEVER && !kw_next(&w, clock_ms + 100000, &a), "stopped: nothing more");
	}
	{
		/* Between LED 3's red and green: only its red off, 40 ms after the red */
		unsigned red_at;

		clock_ms = 1000;
		kw_start(&w, leds, n, NULL, clock_ms);
		for (i = 0; i < 1 + 2 * 6 + 1; i++) {
			clock_ms = kw_due(&w);
			kw_next(&w, clock_ms, &a);
		}
		red_at = clock_ms;
		run(&w, &t, red_at + 5, 0);
		ck(t.n == 2 && t.act[0].kind == KW_ACT_SET && t.act[0].led == 2 && t.act[0].ch == 0 && t.act[0].value == 0 &&
		   t.act[1].kind == KW_ACT_POWER_OFF, "stopped 5 ms after a red: that red off, then the 5 V");
		ck(t.n && t.at[0] - red_at >= KW_GAP_MS, "stopped 5 ms after a write: the next one no sooner than 40 ms after it");
	}
	{
		/* At A's own moment, before the 5 V: nothing to undo */
		clock_ms = 1000;
		kw_start(&w, leds, n, NULL, clock_ms);
		kw_stop(&w, clock_ms);
		ck(w.state == KW_STOPPED && !kw_next(&w, clock_ms + 1000, &a), "stopped before the 5 V: no step at all");
	}
	{
		/* In the clearing: what is still held lit goes off too */
		memset(held, 0, sizeof held);
		held[3][1] = 200;
		held[9][0] = 200;
		clock_ms = 1000;
		kw_start(&w, leds, n, (const unsigned char (*)[3])held, clock_ms);
		for (i = 0; i < 2; i++) {   /* the 5 V, LED 4's green */
			clock_ms = kw_due(&w);
			kw_next(&w, clock_ms, &a);
		}
		run(&w, &t, clock_ms + 10, 0);
		ck(t.n == 2 && t.act[0].kind == KW_ACT_SET && t.act[0].led == 9 && t.act[0].value == 0 &&
		   t.act[1].kind == KW_ACT_POWER_OFF, "stopped in the clearing: LED 10's red, still held, to 0, then the 5 V");
	}
	{
		/* A stop twice is one stop */
		clock_ms = 1000;
		kw_start(&w, leds, n, NULL, clock_ms);
		for (i = 0; i < 3; i++) {
			clock_ms = kw_due(&w);
			kw_next(&w, clock_ms, &a);
		}
		kw_stop(&w, clock_ms + 50);
		kw_stop(&w, clock_ms + 60);
		run(&w, &t, KW_NEVER, 0);
		ck(w.state == KW_STOPPED && t.n == 3, "a second stop changes nothing: two channels off, the 5 V off");
	}

	/* ---- a page held up: one step a pass ---- */
	clock_ms = 1000;
	kw_start(&w, leds, n, NULL, clock_ms);
	run(&w, &t, KW_NEVER, 300);
	rules(&t, leds, n, "a page 300 ms late at every step", false);
	ck(w.state == KW_DONE, "a page 300 ms late: the walk still ends");

	/* ---- one-channel LEDs ---- */
	make_names(4, true);
	n = kw_group((const char (*)[KW_NAME])names, nnames, leds, KW_LEDS);
	clock_ms = 1000;
	kw_start(&w, leds, n, NULL, clock_ms);
	run(&w, &t, KW_NEVER, 0);
	rules(&t, leds, n, "one-channel LEDs", false);
	ck(n == 4 && w.state == KW_DONE && t.n == 2 + 4 * 2, "one-channel LEDs: one write on, one off, each");

	/* ---- none ---- */
	kw_start(&w, leds, 0, NULL, 1000);
	ck(!kw_running(&w) && !kw_next(&w, 2000, &a), "no LEDs: no step, not even the 5 V");

	/* ---- the 5 V at the walk's end (kw_power_end) ---- */
	{
		static const char *const WAS[] = { "out, 1", "out, 0", "in, 1", "not exported", "", NULL };
		bool un;
		size_t s;

		for (s = 0; s < sizeof WAS / sizeof WAS[0]; s++) {
			const char *to = kw_power_end(WAS[s], true, &un);

			ck(to && !strcmp(to, "high") && !un, "on the Smart Pro the controllers' 5 V is never low at the end");
		}
		ck(!strcmp(kw_power_end("out, 1", false, &un), "high") && !un, "elsewhere a pin found high stays high");
		ck(!strcmp(kw_power_end("out, 0", false, &un), "low") && !un, "one found low goes low");
		ck(!strcmp(kw_power_end("in, 1", false, &un), "in") && !un, "one found an input is one again");
		ck(!strcmp(kw_power_end("not exported", false, &un), "low") && un,
		   "one found not exported goes low and back to the kernel (the Brick's)");
		ck(!kw_power_end("", false, &un) && !un, "one the walk never turned on is left alone");
	}

	if (fails) {
		printf("kitwalk: %d of %d checks failed\n", fails, checks);
		return 1;
	}
	printf("kitwalk: %d checks passed\n", checks);
	return 0;
}
