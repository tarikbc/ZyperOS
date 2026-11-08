/* SPDX-License-Identifier: GPL-3.0-only */
/* make check-charging
 *
 * The charging screen (src/charging.h): the charger's kind and its wait for
 * the PHY to name it, the two lines, the time to full in the CC and CV phases
 * and while it settles, and the screen's rules - lit, dark, a press, POWER
 * held, unplugged - across the clock's wrap. No device, no SDL. */
#include "../src/charging.h"

#include <stdio.h>
#include <string.h>

#include "../src/design.h"

static int failures;

#define CHECK(cond, what) do { \
	if (!(cond)) { printf("  FAIL: %s (line %d)\n", what, __LINE__); failures++; } \
} while (0)

static ch_reading rd(int pct, int ma, int mv, int now_mah, int full_mah)
{
	ch_reading r = { pct, ma, mv, now_mah, full_mah, CH_WALL, true, false, false, 0, true };
	return r;
}

/* The estimate after `ms` of steady readings from `t0` */
static int eta_after(const ch_reading *r, unsigned t0, unsigned ms)
{
	ch_eta e = { 0 };

	for (unsigned t = 0; t <= ms; t += 1000) ch_eta_note(&e, t0 + t, r);
	return ch_eta_minutes(&e, t0 + ms, r);
}

static void words_are(const ch_reading *r, int eta, bool settling, const char *speed,
                      const char *when, int line)
{
	char s[96], w[96];

	ch_words(r, eta, settling, s, sizeof s, w, sizeof w);
	if (strcmp(s, speed) || strcmp(w, when)) {
		printf("  FAIL: words \"%s\" / \"%s\", wanted \"%s\" / \"%s\" (line %d)\n",
		       s, w, speed, when, line);
		failures++;
	}
}

/* The kind waits from the cable's coming, or the first reading, for the PHY
 * to name it */
static void kind_wait(unsigned b)
{
	ch_kind_wait w = { 0 };
	ch_reading r = rd(50, 900, 3900, 1500, 3000), q;

	r.kind = CH_UNKNOWN;
	q = r;
	ch_kind_settle(&w, b, &q);
	CHECK(q.kind == CH_PENDING, "not named at first: not said");
	q = r;
	ch_kind_settle(&w, b + CH_KIND_WAIT_MS - 1, &q);
	CHECK(q.kind == CH_PENDING, "nor for the wait");
	q = r;
	q.kind = CH_COMPUTER;
	ch_kind_settle(&w, b + 2000, &q);
	CHECK(q.kind == CH_COMPUTER, "named: said at once");
	q = r;
	ch_kind_settle(&w, b + CH_KIND_WAIT_MS, &q);
	CHECK(q.kind == CH_UNKNOWN, "never named: \"charger\" after the wait");

	/* Out and in again: a new wait from the cable's coming */
	q = r;
	q.online = false;
	ch_kind_settle(&w, b + 20000, &q);
	CHECK(q.kind == CH_UNKNOWN, "out: as it is");
	q = r;
	ch_kind_settle(&w, b + 21000, &q);
	CHECK(q.kind == CH_PENDING, "in again: it waits");
	q = r;
	ch_kind_settle(&w, b + 21000 + CH_KIND_WAIT_MS - 1, &q);
	CHECK(q.kind == CH_PENDING, "for its own wait");
	q = r;
	q.kind = CH_WALL;
	ch_kind_settle(&w, b + 22000, &q);
	CHECK(q.kind == CH_WALL, "then named");
}

/* "Hold A for a full charge": once, after hold-unlock of A down */
static void full_hold(unsigned b)
{
	ch_screen s;

	ch_screen_start(&s, b);
	CHECK(!ch_screen_full(&s, b, true), "A down: not yet");
	CHECK(ch_screen_full_hold(&s, b + DS_MS_HOLD_UNLOCK / 2) > 0.49f, "the bar fills");
	CHECK(!ch_screen_full(&s, b + DS_MS_HOLD_UNLOCK - 1, true), "nor just before");
	CHECK(ch_screen_full(&s, b + DS_MS_HOLD_UNLOCK, true), "held long enough: a full charge");
	CHECK(!ch_screen_full(&s, b + DS_MS_HOLD_UNLOCK + 500, true), "once, though A stays down");
	CHECK(ch_screen_full_hold(&s, b + DS_MS_HOLD_UNLOCK + 500) == 0.0f, "and the bar is gone");
	CHECK(!ch_screen_full(&s, b + 5000, false), "A up");
	CHECK(!ch_screen_full(&s, b + 6000, true), "down again: a new hold");
	CHECK(!ch_screen_full(&s, b + 6000 + DS_MS_HOLD_UNLOCK - 1, true), "counted from its own start");
	CHECK(!ch_screen_full(&s, b + 9000, false) && ch_screen_full_hold(&s, b + 9000) == 0.0f,
	      "let go early: nothing, and the bar empties");
}

static void rules(unsigned b)
{
	ch_screen s;

	ch_screen_start(&s, b);
	CHECK(ch_screen_tick(&s, b + 100, false, false, true) == CS_LIT, "lit at first");
	CHECK(ch_screen_tick(&s, b + DS_MS_HOLD_CHARGE_LIT - 1, false, false, true) == CS_LIT, "lit for its time");
	CHECK(ch_screen_tick(&s, b + DS_MS_HOLD_CHARGE_LIT, false, false, true) == CS_DARK, "then dark");
	CHECK(ch_screen_tick(&s, b + 20000, true, false, true) == CS_LIT, "a press lights it");
	CHECK(ch_screen_tick(&s, b + 20000 + DS_MS_HOLD_CHARGE_LIT, false, false, true) == CS_DARK, "and it goes dark again");

	/* POWER: a press lights, a hold starts */
	CHECK(ch_screen_tick(&s, b + 40000, true, true, true) == CS_LIT, "POWER down lights it");
	CHECK(ch_screen_hold(&s, b + 40000 + DS_MS_HOLD_UNLOCK / 2) > 0.49f, "the bar fills while held");
	CHECK(ch_screen_tick(&s, b + 40000 + DS_MS_HOLD_UNLOCK - 1, false, true, true) == CS_LIT, "not yet");
	CHECK(ch_screen_tick(&s, b + 40000 + DS_MS_HOLD_UNLOCK, false, true, true) == CS_START, "held long enough: start");
	CHECK(ch_screen_tick(&s, b + 90000, false, false, false) == CS_START, "start stays");

	ch_screen_start(&s, b);
	ch_screen_tick(&s, b + 100, true, true, true);
	ch_screen_tick(&s, b + 1500, false, false, true);
	CHECK(ch_screen_hold(&s, b + 1500) == 0.0f, "let go: the bar empties");
	CHECK(ch_screen_tick(&s, b + 1600, true, true, true) == CS_LIT, "down again");
	CHECK(ch_screen_tick(&s, b + 1600 + DS_MS_HOLD_UNLOCK - 1, false, true, true) == CS_LIT,
	      "a new hold counts from its own start");

	/* Unplugged */
	ch_screen_start(&s, b);
	CHECK(ch_screen_tick(&s, b + 500, false, false, false) == CS_UNPLUGGED, "unplugged says so");
	CHECK(ch_screen_tick(&s, b + 500 + DS_MS_HOLD_SHORT - 1, true, false, false) == CS_UNPLUGGED,
	      "a button does not keep it");
	CHECK(ch_screen_tick(&s, b + 500 + DS_MS_HOLD_SHORT, false, false, false) == CS_OFF, "then off");
	CHECK(ch_screen_tick(&s, b + 9000, false, false, true) == CS_OFF, "off stays");
	ch_screen_start(&s, b);
	ch_screen_tick(&s, b + 15000, false, false, true);
	ch_screen_tick(&s, b + 15100, false, false, false);
	CHECK(ch_screen_tick(&s, b + 15200, false, false, true) == CS_LIT, "plugged in again: lit");
	ch_screen_start(&s, b);
	ch_screen_tick(&s, b + 500, false, false, false);
	CHECK(ch_screen_tick(&s, b + 900, true, true, false) == CS_START, "unplugged, POWER: on at once");
	CHECK(ch_screen_tick(&s, b + 9000, false, false, false) == CS_START, "and it stays");
}

int main(void)
{
	ch_reading r;

	printf("charging: the charger's kind\n");
	CHECK(ch_kind_parse("Unknown SDP [DCP] CDP ACA") == CH_WALL, "DCP is a wall charger");
	CHECK(ch_kind_parse("Unknown [SDP] DCP CDP") == CH_COMPUTER, "SDP is a computer");
	CHECK(ch_kind_parse("Unknown SDP DCP [CDP]") == CH_COMPUTER, "CDP is a computer");
	CHECK(ch_kind_parse("[Unknown] SDP DCP") == CH_UNKNOWN, "unknown");
	CHECK(ch_kind_parse("SDP DCP") == CH_UNKNOWN, "no brackets");
	CHECK(ch_kind_parse(NULL) == CH_UNKNOWN, "nothing read");

	printf("charging: the test file's line\n");
	CHECK(ch_fake_parse("62 1480 4120 wall 1", &r) && r.pct == 62 && r.ma == 1480 && r.mv == 4120 &&
	      r.kind == CH_WALL && r.online && !r.full && r.full_mah == 3000 && r.now_mah == 1860,
	      "a whole line");
	CHECK(ch_fake_parse("40 900 3800\n", &r) && r.kind == CH_WALL && r.online, "kind and cable left out");
	CHECK(ch_fake_parse("40 900 3800 computer 0", &r) && r.kind == CH_COMPUTER && !r.online, "a computer, out");
	CHECK(ch_fake_parse("40 900 3800 unknown", &r) && r.kind == CH_UNKNOWN && r.online, "a kind not named");
	CHECK(ch_fake_parse("85 0 4100 wall 1 1", &r) && r.held && r.online, "held at the limit");
	CHECK(ch_fake_parse("85 0 4100 wall 0 1", &r) && !r.held, "not held with the cable out");
	CHECK(ch_fake_parse("85 0 4100 wall 1", &r) && !r.held, "held left out: not held");
	CHECK(ch_fake_parse("100 20 4340 wall", &r) && r.full, "100% is full");
	CHECK(!ch_fake_parse("62 1480", &r) && !ch_fake_parse("", &r) && !ch_fake_parse(NULL, &r) &&
	      !ch_fake_parse("120 1 1", &r), "not a line");

	printf("charging: the lines\n");
	r = rd(62, 1480, 4120, 1860, 3000);
	words_are(&r, 70, false, "Charging · 6.1 W · wall charger", "Full in about 1 h 10 min", __LINE__);
	words_are(&r, 25, false, "Charging · 6.1 W · wall charger", "Full in about 25 min", __LINE__);
	words_are(&r, 120, false, "Charging · 6.1 W · wall charger", "Full in about 2 h", __LINE__);
	words_are(&r, 5, false, "Charging · 6.1 W · wall charger", "Almost full", __LINE__);
	words_are(&r, -1, true, "Charging · 6.1 W · wall charger", "Working out the time to full", __LINE__);
	words_are(&r, -1, false, "Charging · 6.1 W · wall charger", "", __LINE__);
	r.kind = CH_COMPUTER;
	r.ma = 20;
	words_are(&r, -1, false, "Plugged in · computer", "", __LINE__);
	r.kind = CH_UNKNOWN;
	words_are(&r, -1, false, "Plugged in · charger", "", __LINE__);
	r = rd(62, 1480, 4120, 1860, 3000);
	r.kind = CH_PENDING;
	words_are(&r, 70, false, "", "Full in about 1 h 10 min", __LINE__);
	printf("charging: the lines with a charge limit\n");
	r = rd(62, 1480, 4120, 1860, 3000);
	r.limit = 85;
	words_are(&r, 40, false, "Charging · 6.1 W · wall charger", "85% in about 40 min", __LINE__);
	words_are(&r, 70, false, "Charging · 6.1 W · wall charger", "85% in about 1 h 10 min", __LINE__);
	words_are(&r, 60, false, "Charging · 6.1 W · wall charger", "85% in about 1 h", __LINE__);
	words_are(&r, 5, false, "Charging · 6.1 W · wall charger", "Almost at 85%", __LINE__);
	words_are(&r, -1, true, "Charging · 6.1 W · wall charger", "Working out the time to 85%", __LINE__);
	words_are(&r, -1, false, "Charging · 6.1 W · wall charger", "", __LINE__);
	r = rd(85, -5, 4100, 2550, 3000);
	r.limit = 85;
	r.held = true;
	words_are(&r, 0, false, "Kept at 85% · wall charger", "Charge Limit in MENU", __LINE__);
	r.kind = CH_COMPUTER;
	words_are(&r, 0, false, "Kept at 85% · computer", "Charge Limit in MENU", __LINE__);
	r.kind = CH_PENDING;
	words_are(&r, 0, false, "", "Charge Limit in MENU", __LINE__);
	r = rd(83, -5, 4080, 2490, 3000);
	r.limit = 85;
	r.held = true;
	words_are(&r, 0, false, "Kept at 85% · wall charger", "Charge Limit in MENU", __LINE__);
	r.limit = 0;
	words_are(&r, -1, false, "Plugged in · wall charger", "", __LINE__);
	r = rd(62, 1480, 4120, 1860, 3000);
	r.limit = 100;
	words_are(&r, 70, false, "Charging · 6.1 W · wall charger", "Full in about 1 h 10 min", __LINE__);

	r = rd(100, 30, 4340, 3000, 3000);
	r.full = true;
	words_are(&r, 0, false, "Charged · wall charger", "Fully charged", __LINE__);
	r.kind = CH_PENDING;
	words_are(&r, 0, false, "", "Fully charged", __LINE__);

	printf("charging: the time to full\n");
	r = rd(50, 1500, 3900, 1500, 3000);
	CHECK(eta_after(&r, 0, CH_SETTLE_MS - 1000) == -1, "nothing said while it settles");
	/* 1050 mAh at 1.5 A is 42 min; the CV share, 450 mAh falling from
	 * 1.5 A to 150 mA, 46 min more: 88, up to 90 */
	CHECK(eta_after(&r, 0, CH_SETTLE_MS) == 90, "CC then CV: 1 h 30 min");
	r = rd(90, 600, 4300, 2700, 3000);
	/* 300 mAh, 600 mA falling to 150: 300/450 x ln 4 h = 55.5 min, to 60 */
	CHECK(eta_after(&r, 0, 60000) == 60, "in CV: 1 h");
	r = rd(99, 160, 4340, 2990, 3000);
	CHECK(eta_after(&r, 0, 60000) >= 0 && eta_after(&r, 0, 60000) <= 5, "at the very end: almost full");
	r.full = true;
	CHECK(eta_after(&r, 0, 60000) == 0, "full is 0");
	r = rd(50, 1500, 3900, 1500, 3000);
	r.limit = 80;
	/* 900 mAh at 1.5 A, all before the CV phase: 36 min, to 40 */
	CHECK(eta_after(&r, 0, 60000) == 40, "to 80%: 40 min");
	r.limit = 90;
	/* 1050 mAh at 1.5 A is 42 min; then 150 of the CV share's 450, the
	 * current falling from 1.5 A to 1.05 A, 7 min more: 49, to 50 */
	CHECK(eta_after(&r, 0, 60000) == 50, "to 90%: 50 min");
	r.limit = 100;
	CHECK(eta_after(&r, 0, 60000) == 90, "a limit of 100 is none");
	r = rd(85, 600, 4300, 2550, 3000);
	r.limit = 85;
	CHECK(eta_after(&r, 0, 60000) == 0, "at the limit: there");
	r = rd(88, 600, 4300, 2640, 3000);
	r.limit = 90;
	/* In CV, 60 of the 360 left, from 600 mA: 360/450 x ln(600/525) h = 6.4 min, to 10 */
	CHECK(eta_after(&r, 0, 60000) == 10, "to 90% in the CV phase: 10 min");
	r = rd(50, 1500, 3900, -1, 3000);
	CHECK(eta_after(&r, 0, 60000) == -1, "no charge numbers: nothing");
	r = rd(50, 10, 3900, 1500, 3000);
	CHECK(eta_after(&r, 0, 60000) == -1, "nothing going in: nothing");
	{
		/* The average follows a change in about its time constant */
		ch_eta e = { 0 };
		ch_reading a = rd(50, 1500, 3900, 1500, 3000), c = rd(50, 750, 3900, 1500, 3000);
		unsigned t;

		for (t = 0; t <= 60000; t += 1000) ch_eta_note(&e, t, &a);
		for (; t <= 60000 + CH_SMOOTH_MS; t += 1000) ch_eta_note(&e, t, &c);
		CHECK(e.ma > 750 + 750 * 0.30 && e.ma < 750 + 750 * 0.45, "one time constant: about 37% left");
		ch_eta_note(&e, t, &(ch_reading){ 50, 0, 3900, 1500, 3000, CH_WALL, true, false, false, 0, true });
		CHECK(!e.primed, "the current gone: start over");
	}

	printf("charging: the kind, once sure\n");
	kind_wait(1000);
	kind_wait(0xFFFFFFFFu - 3000u);

	printf("charging: a full charge, held for\n");
	full_hold(1000);
	full_hold(0xFFFFFFFFu - 1000u);

	printf("charging: the screen's rules\n");
	rules(1000);
	printf("charging: across the clock's wrap\n");
	rules(0xFFFFFFFFu - 30000u);

	if (failures) {
		printf("charging: %d FAILED\n", failures);
		return 1;
	}
	printf("charging: ok\n");
	return 0;
}
