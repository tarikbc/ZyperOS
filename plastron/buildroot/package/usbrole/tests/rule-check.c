/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * usbrole's rule (src/rule.h), replayed: the boot of 2026-11-10 with a Mac on
 * the cable, as the kernel's log has it, and the plugs around it. The sense
 * at each moment is set by hand; events come when the log says, and between
 * them the rule is asked again when it says to, as usbrole's loop does.
 * Fails on any moment the port would be host while power comes in, a
 * charger detection says a computer or a charger, a reading failed, or a
 * computer is on the bus.
 */
#include <stdio.h>
#include <string.h>

#include "../src/rule.h"

static int fails;

#define CHECK(cond, ...) do { \
	if (!(cond)) { fails++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
	               fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } \
} while (0)

/* A timeline: from each step's time, the port reads `in` until the next */
typedef struct { unsigned at; ur_sense in; } step;

/* Runs it from t0 to `end`, asking at each step and whenever the rule asks
 * to be asked again. Returns the time it first went host, or 0. Every
 * answer is held to the safety rule. */
static unsigned run(const char *what, unsigned t0, const step *st, int n, unsigned end)
{
	ur_state s;
	unsigned now = t0, again = 0, first_host = 0, next_check = 0, powered_at = 0;
	int i = 0, role = UR_DEVICE;

	memset(&s, 0, sizeof s);
	for (; now <= end; now += 10) {
		const ur_sense *in;
		int event = 0, was = role;

		while (i < n && st[i].at <= now) { i++; event = 1; }
		if (!i) continue;
		in = &st[i - 1].in;
		/* What the test knows: the last moment power or a computer was there */
		if (in->vbus != 0 || in->charger || in->computer) powered_at = now;
		if (!event && !(next_check && now >= next_check)) continue;
		role = ur_decide(&s, in, now, &again);
		next_check = again ? now + again : 0;
		if (role == UR_HOST && was == UR_DEVICE) {
			if (!first_host) first_host = now;
			/* Going host: the pin says so, every reading was made, no
			 * power in, no detection, no computer, none for the hold */
			CHECK(in->pin == 1 && in->vbus == 0 && !in->charger && !in->computer,
			      "%s: host at %u with pin %d power %d detection %d computer %d",
			      what, now, in->pin, in->vbus, in->charger, in->computer);
			CHECK(!powered_at || now - powered_at >= UR_HOLD_MS,
			      "%s: host at %u, %u ms after power", what, now, now - powered_at);
			CHECK(now - t0 >= UR_GRACE_MS, "%s: host %u ms after the start", what, now - t0);
		}
	}
	return first_host;
}

#define PIN(p, v, c, k) { (p), (v), (c), (k) }

/* 2026-11-10, a boot with a Mac on the cable (usbrole starts about 5.1 s
 * in): the pin said host at 5.13 s with no power in, went to 0 at 8.12 s,
 * host again at 9.30 s; the old rule went host at 5.13 s and 9.30 s, and
 * the Mac was found as a charging port (CDP) once the port stayed device.
 * That a Mac whose port tries both roles gives power within the hold when
 * the port stays device is what this replays (it did, once the port stayed
 * device); mac_as_sink is the Mac that does not. */
static void mac_boot(void)
{
	static const step st[] = {
		{ 5130, PIN(1, 0, 0, 0) },
		{ 8120, PIN(0, 0, 0, 0) },
		{ 9300, PIN(1, 0, 0, 0) },
		{ 10200, PIN(0, 1, 0, 0) },      /* the Mac's port takes the source side */
		{ 10600, PIN(0, 1, 1, 0) },      /* CDP */
		{ 12300, PIN(1, 1, 1, 0) },      /* the pin flickers with power in */
		{ 12400, PIN(0, 1, 1, 0) },
	};
	unsigned h = run("Mac at boot", 5100, st, (int)(sizeof st / sizeof st[0]), 30000);

	CHECK(!h, "Mac at boot: went host at %u", h);
}

/* The same Mac that holds the sink side for a long while with no power: the
 * rule goes host after the grace and the hold; the Mac shows up on the bus
 * and the port goes device at once, and stays device for that plug */
static void mac_as_sink(void)
{
	static const step st[] = {
		{ 5130, PIN(1, 0, 0, 0) },
		{ 10200, PIN(1, 0, 0, 1) },      /* "Product: Mac" enumerated */
		{ 10300, PIN(1, 0, 0, 0) },      /* back to device: the bus is gone */
		{ 11000, PIN(1, 1, 0, 0) },      /* now the Mac gives power */
		{ 12000, PIN(0, 1, 1, 0) },
		{ 15000, PIN(1, 0, 0, 0) },      /* a flicker without power: same plug */
	};
	ur_state s;
	unsigned again, t;
	int i, hosts = 0, role;

	memset(&s, 0, sizeof s);
	for (t = 5130, i = 0; t < 30000; t += 10) {
		while (i < (int)(sizeof st / sizeof st[0]) - 1 && st[i + 1].at <= t) i++;
		role = ur_decide(&s, &st[i].in, t, &again);
		if (role == UR_HOST && (st[i].in.vbus != 0 || st[i].in.charger) && !s.host)
			CHECK(0, "Mac as sink: host with power in at %u", t);
		if (role == UR_HOST && t > 10200 && st[i].in.computer)
			CHECK(0, "Mac as sink: host with the Mac on the bus at %u", t);
		if (role == UR_HOST) hosts++;
		if (t >= 10200) CHECK(role == UR_DEVICE, "Mac as sink: host again at %u", t);
	}
	CHECK(hosts > 0, "Mac as sink: never host, so the Mac never showed itself (this replay expects one)");
}

/* A computer or a charger: power from the first moment, the pin flickering
 * as the plug settles (2026-11-05): never host */
static void computer_flicker(void)
{
	static const step st[] = {
		{ 0,    PIN(0, 1, 0, 0) },
		{ 100,  PIN(1, 1, 0, 0) },
		{ 300,  PIN(1, 1, 1, 0) },
		{ 4000, PIN(1, 0, 0, 0) },       /* power drops for a moment */
		{ 4300, PIN(1, 1, 1, 0) },
	};

	CHECK(!run("computer", 0, st, 5, 20000), "computer: went host");
}

/* A charger reading that fails: never host */
static void unreadable(void)
{
	static const step a[] = { { 0, PIN(1, -1, 0, 0) } };
	static const step b[] = { { 0, PIN(-1, 0, 0, 0) } };

	CHECK(!run("no power reading", 0, a, 1, 20000), "a failed power reading went host");
	CHECK(!run("no pin reading", 0, b, 1, 20000), "a failed pin reading went host");
}

/* A USB-C DAC plugged in a minute after boot: host, after the hold */
static void dac(void)
{
	static const step st[] = {
		{ 0,     PIN(0, 0, 0, 0) },
		{ 60000, PIN(1, 0, 0, 0) },
	};
	unsigned h = run("DAC", 0, st, 2, 70000);

	CHECK(h >= 60000 + UR_HOLD_MS && h <= 60000 + UR_HOLD_MS + 20, "DAC: host at %u", h);
}

/* A DAC in at boot: not before the grace and the hold */
static void dac_at_boot(void)
{
	static const step st[] = { { 1000, PIN(1, 0, 0, 0) } };
	unsigned h = run("DAC at boot", 1000, st, 1, 20000);

	CHECK(h >= 1000 + UR_GRACE_MS + UR_HOLD_MS && h <= 1000 + UR_GRACE_MS + UR_HOLD_MS + 20,
	      "DAC at boot: host at %u", h);
}

/* A partner that tries both roles, the pin flipping every 40 ms with no
 * power: never held long enough */
static void toggler(void)
{
	step st[400];
	int i;

	for (i = 0; i < 400; i++) {
		st[i].at = 6000 + (unsigned)i * 40;
		st[i].in = (ur_sense)PIN(i & 1, 0, 0, 0);
	}
	CHECK(!run("toggler", 6000, st, 400, 6000 + 400 * 40), "a toggling partner went host");
}

/* The computer's cable out, a DAC in: a new plug once the port has been
 * empty long enough, and not before */
static void computer_then_dac(void)
{
	static const step quick[] = {
		{ 0,     PIN(0, 1, 1, 0) },
		{ 20000, PIN(0, 0, 0, 0) },              /* out */
		{ 20500, PIN(1, 0, 0, 0) },              /* a DAC 0.5 s later */
	};
	static const step slow[] = {
		{ 0,     PIN(0, 1, 1, 0) },
		{ 20000, PIN(0, 0, 0, 0) },
		{ 25000, PIN(1, 0, 0, 0) },              /* 5 s later */
	};
	unsigned h = run("computer then DAC, quick", 0, quick, 3, 40000);

	CHECK(!h, "the computer's plug was forgotten after 0.5 s (host at %u)", h);
	h = run("computer then DAC", 0, slow, 3, 40000);
	CHECK(h == 25000 + UR_HOLD_MS, "a DAC after the computer was unplugged: host at %u", h);
}

/* Host, then the DAC unplugged: device */
static void dac_out(void)
{
	ur_state s;
	unsigned again;
	ur_sense in = PIN(1, 0, 0, 0);

	memset(&s, 0, sizeof s);
	ur_decide(&s, &in, 0, &again);
	ur_decide(&s, &in, UR_GRACE_MS, &again);
	CHECK(ur_decide(&s, &in, UR_GRACE_MS + UR_HOLD_MS, &again) == UR_HOST, "the DAC: not host");
	in.vbus = 1;                                  /* the port's own power, while host */
	CHECK(ur_decide(&s, &in, UR_GRACE_MS + UR_HOLD_MS + 10, &again) == UR_HOST,
	      "the port's own power ended host");
	in.pin = 0;
	CHECK(ur_decide(&s, &in, UR_GRACE_MS + UR_HOLD_MS + 20, &again) == UR_DEVICE, "the DAC out: still host");
}

int main(void)
{
	mac_boot();
	mac_as_sink();
	computer_flicker();
	unreadable();
	dac();
	dac_at_boot();
	toggler();
	computer_then_dac();
	dac_out();
	if (fails) {
		fprintf(stderr, "usbrole rule-check: %d failed\n", fails);
		return 1;
	}
	printf("usbrole rule-check: the Mac's boot of 2026-11-10 stays device, power or a computer on the bus "
	       "keep a plug device, a DAC is host after the hold\n");
	return 0;
}
