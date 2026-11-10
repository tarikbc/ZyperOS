/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * usbrole's rule (src/rule.h), replayed. First the nine plugs of a Mac that
 * usbrole's data build traced on 2026-11-10 (/run/usbrole/trace.log), as
 * the readings came: none of them may make the port a host, whatever the
 * launcher is asked. Then the launcher's question: a DAC is host only after
 * Power It for its own plug, never for another plug's, never while power
 * comes in, and a computer that turns up on the bus ends it.
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
 * `grant_all`: the launcher says Power It the moment it may be asked, as a
 * player who presses A at once would. Returns how many times it went host;
 * every decision held to the safety rule. */
static int run(const char *what, const step *st, int n, unsigned end, int grant_all,
               unsigned *asked_at)
{
	ur_state s;
	unsigned now, again;
	int i = 0, role = UR_DEVICE, hosts = 0;

	memset(&s, 0, sizeof s);
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
			CHECK(s.granted == s.plug, "%s: host at %u without Power It for plug %u", what, now, s.plug);
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

	/* Nobody answers: never host */
	CHECK(!run("the Mac, nobody answers", MAC, n, 32115000, 0, &asked), "the Mac's plugs: host");
	/* Plugs 3 and 4 came within 5 s of a powered plug: not asked about
	 * (they are the computer's); plug 7 came after half a minute: asked */
	CHECK(asked >= 32055817 && asked < 32055817 + 100, "the Mac's plugs: first asked at %u, not plug 7", asked);
}

/* The Mac's plug 7, answered "It's a Computer": nothing; answered
 * "Power It" by mistake, the Mac shows up on the bus and the port goes back
 * to device for the rest of the plug */
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
		if (t == 41000) CHECK(ur_grant(&s, s.plug), "Power It on the Mac's plug was refused");
		role = ur_decide(&s, &st[i - 1].in, t, &again);
		if (role == UR_HOST && was == UR_DEVICE) hosts++;
		if (t >= 42500) CHECK(role == UR_DEVICE, "the Mac on the bus: host at %u", t);
		was = role;
	}
	CHECK(hosts == 1, "Power It by mistake: host %d times", hosts);
	CHECK(!s.can_ask, "the Mac's plug may still be asked about");
}

/* A USB-C DAC a minute after boot: asked about after the grace, host the
 * moment Power It comes for its plug, and back to device when it goes */
static void dac(void)
{
	static const step st[] = {
		{ 0,     S(0, 0, 0, 0) },
		{ 60000, S(1, 0, 0, 0) },
		{ 90000, S(0, 0, 0, 0) },       /* out */
		{ 91000, S(1, 0, 0, 0) },       /* in again, a second later */
	};
	ur_state s;
	unsigned again, t, first;
	int i = 0, role;

	memset(&s, 0, sizeof s);
	for (t = 0; t <= 95000; t += 10) {
		while (i < 4 && st[i].at <= t) i++;
		role = ur_decide(&s, &st[i - 1].in, t, &again);
		if (t == 59990) CHECK(!ur_grant(&s, 1), "Power It before anything wanted power was kept");
		if (t == 60000) {
			first = s.plug;
			CHECK(s.can_ask, "the DAC: not asked about");
			CHECK(role == UR_DEVICE, "the DAC: host before Power It");
			CHECK(!ur_grant(&s, first + 1), "Power It for another plug was kept");
			CHECK(ur_grant(&s, first), "Power It for the DAC's plug was refused");
		}
		if (t == 60010) CHECK(role == UR_HOST, "the DAC: not host after Power It");
		if (t > 60010 && t < 90000) CHECK(role == UR_HOST, "the DAC: host ended at %u", t);
		if (t >= 90000) CHECK(role == UR_DEVICE, "the DAC out, or in again unasked: host at %u", t);
		if (t == 91000) {
			CHECK(s.plug == first + 1, "in again: the same plug number");
			CHECK(s.can_ask, "in again: not asked about");
		}
	}
}

/* At boot: not asked about in the grace */
static void dac_at_boot(void)
{
	static const step st[] = { { 1000, S(1, 0, 0, 0) } };
	unsigned asked;

	CHECK(!run("DAC at boot", st, 1, 1000 + UR_GRACE_MS + 5000, 0, &asked), "DAC at boot: host unasked");
	CHECK(asked == 1000 + UR_GRACE_MS, "DAC at boot: asked at %u", asked);
	CHECK(run("DAC at boot, Power It", st, 1, 1000 + UR_GRACE_MS + 5000, 1, NULL) == 1,
	      "DAC at boot: not host after Power It");
}

/* Power It, then power comes in before the role changes: device */
static void power_after_grant(void)
{
	ur_state s;
	unsigned again;
	ur_sense dac = S(1, 0, 0, 0), powered = S(1, 1, 0, 0);

	memset(&s, 0, sizeof s);
	ur_decide(&s, &dac, 0, &again);
	ur_decide(&s, &dac, UR_GRACE_MS, &again);
	CHECK(ur_grant(&s, s.plug), "Power It refused");
	CHECK(ur_decide(&s, &powered, UR_GRACE_MS + 10, &again) == UR_DEVICE, "host with power in");
	CHECK(ur_decide(&s, &dac, UR_GRACE_MS + 20, &again) == UR_DEVICE,
	      "a plug that had power in went host on an old Power It");
}

/* Every plug of a computer or a charger, and failed readings, with Power
 * It pressed at every chance: never host */
static void never(void)
{
	static const step computer[] = {
		{ 0,    S(0, 1, 0, 0) }, { 100, S(1, 1, 0, 0) }, { 300, S(1, 1, 1, 0) },
		{ 4000, S(1, 0, 0, 0) }, { 4300, S(1, 1, 1, 0) },
	};
	static const step no_power_reading[] = { { 0, S(1, -1, 0, 0) } };
	static const step no_pin_reading[] = { { 0, S(-1, 0, 0, 0) } };
	static const step quick_after_computer[] = {
		{ 0, S(0, 1, 1, 0) }, { 20000, S(0, 0, 0, 0) }, { 20500, S(1, 0, 0, 0) },
	};

	CHECK(!run("computer", computer, 5, 30000, 1, NULL), "a computer went host");
	CHECK(!run("no power reading", no_power_reading, 1, 30000, 1, NULL), "a failed power reading went host");
	CHECK(!run("no pin reading", no_pin_reading, 1, 30000, 1, NULL), "a failed pin reading went host");
	CHECK(!run("quick after a computer", quick_after_computer, 3, 40000, 1, NULL),
	      "a plug 0.5 s after a computer's went host");
	CHECK(!run("the Mac, Power It at every chance", MAC, (int)(sizeof MAC / sizeof MAC[0]),
	           32055817 - 1, 1, NULL), "the Mac's first six plugs went host on Power It");
}

/* 2026-11-10, the test after option C: Mac plugs replugged a few seconds
 * apart, the PHY's detection (CDP) lingering 1.5 s after the power went.
 * A plug 4.5 s after the power went is still the last one's, not asked
 * about; one 5.2 s after is a new plug, asked about. */
static void lingering_detection(void)
{
	static const step soon[] = {
		{ 0,     S(0, 1, 1, 0) },        /* the Mac giving power, CDP */
		{ 20000, S(0, 0, 1, 0) },        /* out: power gone, CDP lingers */
		{ 21500, S(0, 0, 0, 0) },
		{ 24500, S(1, 0, 0, 0) },        /* in again, the Mac waits: 4.5 s after */
	};
	static const step later[] = {
		{ 0,     S(0, 1, 1, 0) },
		{ 20000, S(0, 0, 1, 0) },
		{ 21500, S(0, 0, 0, 0) },
		{ 25200, S(1, 0, 0, 0) },        /* 5.2 s after the power went */
	};
	unsigned asked;

	CHECK(!run("replugged 4.5 s after", soon, 4, 40000, 0, &asked) && !asked,
	      "a plug 4.5 s after the power went was asked about at %u", asked);
	CHECK(!run("replugged 5.2 s after", later, 4, 40000, 0, &asked) && asked == 25200,
	      "a plug 5.2 s after the power went was not asked about (%u)", asked);
	/* And with Power It at every chance, the first never goes host */
	CHECK(!run("replugged 4.5 s after, Power It", soon, 4, 40000, 1, NULL),
	      "a plug 4.5 s after a computer's power went host");
}

int main(void)
{
	lingering_detection();
	mac_replugs();
	mac_power_it_by_mistake();
	dac();
	dac_at_boot();
	power_after_grant();
	never();
	if (fails) {
		fprintf(stderr, "usbrole rule-check: %d failed\n", fails);
		return 1;
	}
	printf("usbrole rule-check: the Mac's nine plugs of 2026-11-10 never host, asked about once; "
	       "a DAC host only on Power It for its own plug; never with power in\n");
	return 0;
}
