/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * usbrole's rule (src/rule.h), replayed. The nine plugs of a Mac that
 * usbrole's data build traced on 2026-11-10, as the readings came: never host
 * unanswered. The quick swap of that day (the Mac's cable out, the TP-Link in
 * a second later) and a plug made during a game: asked about, and host on
 * the answer. Power Automatically: host after the settle. And never with
 * power in, a charger detection, a failed reading or a computer on the bus.
 */
#include <stdio.h>
#include <string.h>

#include "../src/rule.h"

static int fails;

#define CHECK(cond, ...) do { \
	if (!(cond)) { fails++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
	               fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } \
} while (0)

typedef struct { unsigned at; ur_sense in; } step;

#define S(p, v, c, k) { (p), (v), (c), (k) }

/* Readings from t0 to `end`, the rule asked at each change and every 50 ms
 * (usbrole asks at each event; asking more often must change nothing).
 * `grant_all`: the launcher says USB Accessory the moment it may be asked, as a
 * player who presses A at once would. Returns how many times it went host;
 * every decision held to the safety rule. */
static int run(const char *what, const step *st, int n, unsigned end, int grant_all,
               int auto_mode, unsigned *asked_at)
{
	ur_state s;
	unsigned now, again;
	int i = 0, role = UR_DEVICE, hosts = 0;

	memset(&s, 0, sizeof s);
	s.auto_mode = auto_mode;
	if (asked_at) *asked_at = 0;
	for (now = st[0].at; now <= end; now += 10) {
		const ur_sense *in;
		int was = role;

		while (i < n && st[i].at <= now) i++;
		in = &st[i - 1].in;
		role = ur_decide(&s, in, now, &again);
		if (s.can_ask && asked_at && !*asked_at) *asked_at = now;
		if (s.can_ask && grant_all) {
			ur_grant(&s, s.plug);
			role = ur_decide(&s, in, now, &again);
		}
		if (role == UR_HOST && was == UR_DEVICE) {
			hosts++;
			CHECK(in->pin == 1 && in->vbus == 0 && !in->charger && !in->computer,
			      "%s: host at %u with pin %d power %d detection %d computer %d",
			      what, now, in->pin, in->vbus, in->charger, in->computer);
			CHECK(s.granted == s.plug, "%s: host at %u without an answer for plug %u", what, now, s.plug);
		}
	}
	return hosts;
}

/* The data build's trace, 2026-11-10 (times in ms of uptime): the Mac on
 * the cable from boot; then Tarik's plugs in the order he made them. The
 * port is never host and the launcher would ask about one plug only, the
 * seventh, where the Mac's port sat on the sink side after the cable had
 * been out for half a minute. */
static const step MAC[] = {
	{ 1115,     S(0, 1, 0, 0) },        /* booted with the Mac giving power */
	{ 2330,     S(0, 1, 1, 0) },        /* SDP */
	{ 31920225, S(0, 0, 1, 0) },        /* out */
	{ 31921330, S(0, 0, 0, 0) },
	{ 31945071, S(0, 1, 0, 0) },        /* plug 1, side A: power at once */
	{ 31945166, S(0, 1, 1, 0) },        /* CDP */
	{ 31955720, S(0, 0, 1, 0) },        /* out */
	{ 31955887, S(0, 0, 0, 0) },
	{ 31962090, S(0, 1, 0, 0) },        /* plug 2, side B */
	{ 31962179, S(0, 1, 1, 0) },
	{ 31969184, S(0, 0, 1, 0) },        /* out */
	{ 31970608, S(0, 0, 0, 0) },
	{ 31971407, S(1, 0, 0, 0) },        /* plug 3, side A: the Mac waits, 16.1 s */
	{ 31987526, S(0, 0, 0, 0) },        /* out */
	{ 31989092, S(1, 0, 0, 0) },        /* plug 4, side A: waits, 10.3 s */
	{ 31999346, S(0, 0, 0, 0) },        /* out */
	{ 32001438, S(0, 1, 0, 0) },        /* plug 5, side A: power */
	{ 32001732, S(0, 1, 1, 0) },
	{ 32010984, S(0, 0, 1, 0) },        /* out */
	{ 32012421, S(0, 0, 0, 0) },
	{ 32013342, S(0, 1, 0, 0) },        /* plug 6, side B: power */
	{ 32013436, S(0, 1, 1, 0) },
	{ 32026789, S(0, 0, 1, 0) },        /* out */
	{ 32028420, S(0, 0, 0, 0) },
	{ 32055817, S(1, 0, 0, 0) },        /* plug 7, back in: the Mac waits */
	{ 32107539, S(0, 0, 0, 0) },        /* it turns to the source side by itself */
	{ 32109030, S(0, 1, 0, 0) },
	{ 32109892, S(0, 1, 1, 0) },        /* CDP: the link */
};

static void mac_replugs(void)
{
	int n = (int)(sizeof MAC / sizeof MAC[0]);
	unsigned asked;

	/* Nobody answers: never host. Every plug where the Mac waited may be
	 * asked about, the first at plug 3, once its detection had cleared:
	 * no plug inherits the last one's power (2026-11-10) */
	CHECK(!run("the Mac, nobody answers", MAC, n, 32115000, 0, 0, &asked), "the Mac's plugs: host");
	CHECK(asked >= 31971407 && asked < 31971407 + 20, "the Mac's plugs: first asked at %u, not plug 3", asked);
	/* Answered at every chance: host only under the safety rule (run holds
	 * every one), never while the Mac gives power */
	run("the Mac, answered at every chance", MAC, n, 32115000, 1, 0, NULL);
}

/* 2026-11-10, in Crazy Taxi 2: the Mac cable out, the TP-Link in a second
 * later, while the detection (CDP) still lingered. Not asked about while it
 * lingers; asked once it clears; host on the answer */
static void quick_swap(void)
{
	static const step st[] = {
		{ 0,     S(0, 1, 1, 0) },        /* the Mac giving power, CDP */
		{ 20000, S(0, 0, 1, 0) },        /* out: power gone, CDP lingers */
		{ 21000, S(1, 0, 1, 0) },        /* the adapter in */
		{ 21500, S(1, 0, 0, 0) },        /* the detection clears */
	};
	unsigned asked;

	CHECK(!run("the swap, nobody answers", st, 4, 60000, 0, 0, &asked), "the swap went host unasked");
	CHECK(asked == 21500, "the swap: asked at %u, not when the detection cleared", asked);
	CHECK(run("the swap, USB Accessory", st, 4, 60000, 1, 0, NULL) == 1, "the swap: not host on the answer");
}

/* An adapter plugged in during a game: it waits, unpowered, for as long as
 * the game lasts, and is powered on the answer the shelf gets after it */
static void plugged_in_a_game(void)
{
	static const step st[] = { { 0, S(0, 0, 0, 0) }, { 40000, S(1, 0, 0, 0) } };
	ur_state s;
	unsigned again, t;
	int i = 0;

	memset(&s, 0, sizeof s);
	for (t = 0; t <= 100000; t += 10) {
		while (i < 2 && st[i].at <= t) i++;
		CHECK(ur_decide(&s, &st[i - 1].in, t, &again) == UR_DEVICE, "the game's plug went host unasked at %u", t);
	}
	CHECK(s.can_ask && ur_grant(&s, s.plug), "the game's plug, back on the shelf: refused");
	CHECK(ur_decide(&s, &st[1].in, 100010, &again) == UR_HOST, "the game's plug: not host on the answer");
}

/* USB Accessory by mistake on the Mac's waiting plug: the Mac shows up on
 * the bus and the port goes back to device for the rest of the plug */
static void mac_power_it_by_mistake(void)
{
	static const step st[] = {
		{ 0,     S(0, 0, 0, 0) },
		{ 40000, S(1, 0, 0, 0) },        /* the Mac waits */
		{ 42500, S(1, 0, 0, 1) },        /* fed: "Product: Mac" on the bus */
		{ 42600, S(1, 0, 0, 0) },
		{ 60000, S(0, 0, 0, 0) },
		{ 61500, S(0, 1, 1, 0) },        /* it gives power: the link */
	};
	ur_state s;
	unsigned again, t;
	int i = 0, role, hosts = 0, was = UR_DEVICE;

	memset(&s, 0, sizeof s);
	for (t = 0; t <= 70000; t += 10) {
		while (i < 6 && st[i].at <= t) i++;
		if (t == 41000) CHECK(ur_grant(&s, s.plug), "USB Accessory on the Mac's plug was refused");
		role = ur_decide(&s, &st[i - 1].in, t, &again);
		if (t > 42600 && t < 60000) CHECK(!ur_grant(&s, s.plug), "the Mac's plug granted again at %u", t);
		if (role == UR_HOST && was == UR_DEVICE) hosts++;
		if (t >= 42500) CHECK(role == UR_DEVICE, "the Mac on the bus: host at %u", t);
		was = role;
	}
	CHECK(hosts == 1, "USB Accessory by mistake: host %d times", hosts);
}

/* A USB-C DAC a minute after boot: asked about after the grace, host the
 * moment USB Accessory comes for its plug, and back to device when it goes */
static void dac(void)
{
	static const step st[] = {
		{ 0,     S(0, 0, 0, 0) },
		{ 60000, S(1, 0, 0, 0) },
		{ 90000, S(0, 0, 0, 0) },       /* out */
		{ 91000, S(1, 0, 0, 0) },       /* in again, a second later */
	};
	ur_state s;
	unsigned again, t, first = 0;
	int i = 0, role;

	memset(&s, 0, sizeof s);
	for (t = 0; t <= 95000; t += 10) {
		while (i < 4 && st[i].at <= t) i++;
		role = ur_decide(&s, &st[i - 1].in, t, &again);
		if (t == 59990) CHECK(!ur_grant(&s, 1), "an answer before anything wanted power was kept");
		if (t == 60000) {
			first = s.plug;
			CHECK(s.can_ask, "the DAC: not asked about");
			CHECK(role == UR_DEVICE, "the DAC: host before the answer");
			CHECK(!ur_grant(&s, first + 1), "an answer for another plug was kept");
			CHECK(ur_grant(&s, first), "the answer for the DAC's plug was refused");
		}
		if (t == 60010) CHECK(role == UR_HOST, "the DAC: not host after the answer");
		if (t > 60010 && t < 90000) CHECK(role == UR_HOST, "the DAC: host ended at %u", t);
		if (t >= 90000) CHECK(role == UR_DEVICE, "the DAC out, or in again unasked: host at %u", t);
		if (t == 91000) {
			CHECK(s.plug == first + 1, "in again: the same plug number");
			CHECK(s.can_ask, "in again a second later: not asked about");
		}
	}
}

/* At boot: not asked about in the grace */
static void dac_at_boot(void)
{
	static const step st[] = { { 1000, S(1, 0, 0, 0) } };
	unsigned asked;

	CHECK(!run("DAC at boot", st, 1, 1000 + UR_GRACE_MS + 5000, 0, 0, &asked), "DAC at boot: host unasked");
	CHECK(asked == 1000 + UR_GRACE_MS, "DAC at boot: asked at %u", asked);
	CHECK(run("DAC at boot, answered", st, 1, 1000 + UR_GRACE_MS + 5000, 1, 0, NULL) == 1,
	      "DAC at boot: not host after the answer");
}

/* Power Automatically: a DAC is host after its settle with no answer; the
 * Mac waiting is too, shows up on the bus, and goes back to device for the
 * rest of its plug; a charger and failed readings never */
static void automatic(void)
{
	static const step dac_st[] = { { 0, S(0, 0, 0, 0) }, { 10000, S(1, 0, 0, 0) } };
	static const step mac_st[] = {
		{ 0,     S(0, 0, 0, 0) },
		{ 10000, S(1, 0, 0, 0) },                          /* the Mac waits */
		{ 10000 + UR_SETTLE_MS + 1900, S(1, 0, 0, 1) },    /* fed, on the bus */
		{ 10000 + UR_SETTLE_MS + 2000, S(1, 0, 0, 0) },    /* still waiting */
	};
	static const step charger[] = { { 0, S(0, 1, 1, 0) }, { 5000, S(1, 1, 1, 0) } };
	static const step no_power_reading[] = { { 0, S(1, -1, 0, 0) } };
	unsigned asked;
	int h;

	/* The settle: device until it has passed, host the moment it has */
	{
		ur_state a;
		unsigned again;
		ur_sense none = S(0, 0, 0, 0), want = S(1, 0, 0, 0);

		memset(&a, 0, sizeof a);
		a.auto_mode = 1;
		ur_decide(&a, &none, 0, &again);
		CHECK(ur_decide(&a, &want, 10000, &again) == UR_DEVICE && again == UR_SETTLE_MS,
		      "auto: host before the settle, or no time to look again (%u)", again);
		CHECK(ur_decide(&a, &want, 10000 + UR_SETTLE_MS - 1, &again) == UR_DEVICE, "auto: host 1 ms early");
		CHECK(ur_decide(&a, &want, 10000 + UR_SETTLE_MS, &again) == UR_HOST, "auto: not host after the settle");
	}
	h = run("auto, a DAC", dac_st, 2, 30000, 0, 1, &asked);
	CHECK(h == 1, "auto: the DAC host %d times", h);
	h = run("auto, the Mac", mac_st, 4, 60000, 0, 1, NULL);
	CHECK(h == 1, "auto: the Mac fed %d times in one plug", h);
	CHECK(!run("auto, a charger", charger, 2, 30000, 0, 1, NULL), "auto: a charger went host");
	CHECK(!run("auto, no power reading", no_power_reading, 1, 30000, 0, 1, NULL), "auto: a failed reading went host");
}

/* The Mac fed by mistake shows up on the bus; the cable out, a DAC in: the
 * DAC's plug knows nothing of the Mac's, and is asked about */
static void computer_then_dac(void)
{
	ur_state s;
	unsigned again;
	ur_sense want = S(1, 0, 0, 0), mac = S(1, 0, 0, 1), none = S(0, 0, 0, 0);

	memset(&s, 0, sizeof s);
	ur_decide(&s, &none, 0, &again);
	ur_decide(&s, &want, 10000, &again);
	CHECK(ur_grant(&s, s.plug), "the first plug refused");
	CHECK(ur_decide(&s, &want, 10010, &again) == UR_HOST, "the first plug not host");
	CHECK(ur_decide(&s, &mac, 12000, &again) == UR_DEVICE, "the Mac on the bus stayed host");
	CHECK(!s.can_ask, "the Mac's plug may still be asked about");
	ur_decide(&s, &none, 13000, &again);
	ur_decide(&s, &want, 13500, &again);
	CHECK(s.can_ask, "the DAC after the Mac: not asked about");
}

/* Answered, then power comes in before the role changes: device */
static void power_after_grant(void)
{
	ur_state s;
	unsigned again;
	ur_sense dac = S(1, 0, 0, 0), powered = S(1, 1, 0, 0);

	memset(&s, 0, sizeof s);
	ur_decide(&s, &dac, 0, &again);
	ur_decide(&s, &dac, UR_GRACE_MS, &again);
	CHECK(ur_grant(&s, s.plug), "the answer refused");
	CHECK(ur_decide(&s, &powered, UR_GRACE_MS + 10, &again) == UR_DEVICE, "host with power in");
	CHECK(ur_decide(&s, &dac, UR_GRACE_MS + 20, &again) == UR_DEVICE,
	      "a plug went host on an answer from before the power came");
}

/* A computer or a charger, and failed readings, answered at every chance:
 * never host */
static void never(void)
{
	static const step computer[] = {
		{ 0,    S(0, 1, 0, 0) }, { 100, S(1, 1, 0, 0) }, { 300, S(1, 1, 1, 0) },
		{ 4000, S(1, 1, 1, 0) }, { 4300, S(0, 1, 1, 0) },
	};
	static const step no_power_reading[] = { { 0, S(1, -1, 0, 0) } };
	static const step no_pin_reading[] = { { 0, S(-1, 0, 0, 0) } };

	CHECK(!run("computer", computer, 5, 30000, 1, 0, NULL), "a computer went host");
	CHECK(!run("no power reading", no_power_reading, 1, 30000, 1, 0, NULL), "a failed power reading went host");
	CHECK(!run("no pin reading", no_pin_reading, 1, 30000, 1, 0, NULL), "a failed pin reading went host");
}

int main(void)
{
	mac_replugs();
	quick_swap();
	plugged_in_a_game();
	mac_power_it_by_mistake();
	dac();
	dac_at_boot();
	automatic();
	computer_then_dac();
	power_after_grant();
	never();
	if (fails) {
		fprintf(stderr, "usbrole rule-check: %d failed\n", fails);
		return 1;
	}
	printf("usbrole rule-check: the Mac's plugs of 2026-11-10 never host unanswered; the quick swap and a "
	       "game's plug asked and powered on the answer; Power Automatically after its settle; never with "
	       "power in, a detection, a failed reading or a computer on the bus\n");
	return 0;
}
